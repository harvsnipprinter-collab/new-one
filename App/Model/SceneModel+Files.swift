// SculptForge 3D - project management, autosave / recovery, import, export and image rendering
import Foundation
import UIKit
import simd

struct ProjectEntry: Identifiable {
    var id: URL { url }
    let url: URL
    let name: String
    let modified: Date
    let sizeBytes: Int
}

enum ExportFormat: Int32, CaseIterable, Identifiable {
    case stl = 0, obj = 1, ply = 2, glb = 3, threeMF = 4
    var id: Int32 { rawValue }
    var title: String { ["STL", "OBJ", "PLY", "GLB", "3MF"][Int(rawValue)] }
    var fileExtension: String { ["stl", "obj", "ply", "glb", "3mf"][Int(rawValue)] }
    /// Formats intended for slicers are written in millimetres.
    var usesMillimetres: Bool { self == .stl || self == .threeMF }
}

extension SceneModel {
    static let projectExtension = "sculptforge"

    var documentsURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first ?? FileManager.default.temporaryDirectory
    }
    var autosaveURL: URL { documentsURL.appendingPathComponent(".autosave." + SceneModel.projectExtension) }
    private var thumbnailsURL: URL { documentsURL.appendingPathComponent(".thumbnails", isDirectory: true) }
    func thumbnailURL(for project: URL) -> URL {
        thumbnailsURL.appendingPathComponent(project.deletingPathExtension().lastPathComponent + ".png")
    }
    private func projectURL(named name: String) -> URL {
        documentsURL.appendingPathComponent(SceneModel.safeFileName(name)).appendingPathExtension(SceneModel.projectExtension)
    }
    static func safeFileName(_ name: String) -> String {
        let cleaned = name.components(separatedBy: CharacterSet(charactersIn: "/\\:?%*|\"<>\n")).joined(separator: "_")
            .trimmingCharacters(in: .whitespaces)
        return cleaned.isEmpty ? "Untitled" : String(cleaned.prefix(80))
    }

    func projectList() -> [ProjectEntry] {
        let keys: [URLResourceKey] = [.contentModificationDateKey, .fileSizeKey]
        let urls = (try? FileManager.default.contentsOfDirectory(at: documentsURL, includingPropertiesForKeys: keys, options: [])) ?? []
        return urls.filter { $0.pathExtension == SceneModel.projectExtension && !$0.lastPathComponent.hasPrefix(".") }
            .map { url in
                let v = try? url.resourceValues(forKeys: Set(keys))
                return ProjectEntry(url: url, name: url.deletingPathExtension().lastPathComponent,
                                    modified: v?.contentModificationDate ?? .distantPast, sizeBytes: v?.fileSize ?? 0)
            }
            .sorted { $0.modified > $1.modified }
    }

    // MARK: - app state stored inside the project

    private func currentAppState() -> ProjectAppState {
        var s = ProjectAppState()
        s.render = render
        s.cameraTarget = camera.target
        s.cameraDistance = camera.distance
        s.cameraYaw = camera.yaw
        s.cameraPitch = camera.pitch
        s.cameraFov = camera.fovDegrees
        s.cameraOrtho = camera.orthographic
        s.bookmarks = bookmarks
        s.brushPresets = brushPresets
        s.millimetresPerUnit = settings.millimetresPerUnit
        s.referenceImages = referenceImages
        return s
    }

    private func applyAppState(json: String) {
        // An older project or invalid app state must not leave a different project's
        // reference images visible in the newly opened scene.
        referenceImages = []
        selectedReferenceID = nil
        guard let data = json.data(using: .utf8), let s = try? JSONDecoder().decode(ProjectAppState.self, from: data) else { return }
        render = s.render
        camera.target = s.cameraTarget
        camera.distance = max(0.01, s.cameraDistance)
        camera.yaw = s.cameraYaw
        camera.pitch = s.cameraPitch
        cameraFov = max(5, min(120, s.cameraFov))
        cameraOrtho = s.cameraOrtho
        bookmarks = s.bookmarks
        referenceImages = s.referenceImages ?? []
        for p in s.brushPresets where !brushPresets.contains(where: { $0.id == p.id }) { brushPresets.append(p) }
        settings.millimetresPerUnit = s.millimetresPerUnit
    }

    // MARK: - new / open / save

    func newProject() {
        guard !busy else { return }
        endStroke()
        try? FileManager.default.removeItem(at: autosaveURL)
        startDefaultScene()
    }

    private func write(to url: URL) -> Bool {
        if let data = try? JSONEncoder().encode(currentAppState()), let json = String(data: data, encoding: .utf8) {
            sf_set_app_state(scene, json)
        }
        return sf_save_project(scene, url.path) != 0
    }

    @discardableResult
    func save() -> Bool {
        guard !busy else { return false }
        guard let url = documentURL else { return saveAs(name: projectName) }
        endStroke()
        guard write(to: url) else { message = "Save failed: " + lastError; return false }
        isDirty = false
        try? FileManager.default.removeItem(at: autosaveURL)
        writeThumbnail(for: url)
        projectListVersion += 1
        return true
    }

    @discardableResult
    func saveAs(name: String) -> Bool {
        guard !busy else { return false }
        endStroke()
        let url = projectURL(named: name)
        guard write(to: url) else { message = "Save failed: " + lastError; return false }
        documentURL = url
        projectName = url.deletingPathExtension().lastPathComponent
        isDirty = false
        try? FileManager.default.removeItem(at: autosaveURL)
        writeThumbnail(for: url)
        projectListVersion += 1
        return true
    }

    func open(_ url: URL, asRecovery: Bool = false) {
        guard !busy else { return }
        endStroke()
        guard sf_load_project(scene, url.path) != 0 else {
            message = "Could not open \(url.lastPathComponent): " + lastError
            return
        }
        applyAppState(json: String(cString: sf_get_app_state(scene)))
        if asRecovery {
            documentURL = nil
            projectName = "Recovered"
            isDirty = true
        } else {
            documentURL = url
            projectName = url.deletingPathExtension().lastPathComponent
            isDirty = false
        }
        selection = 0
        markAllForUpload()
        refresh()
    }

    func duplicateProject(_ url: URL) {
        let base = url.deletingPathExtension().lastPathComponent
        var n = 2
        var dst = projectURL(named: base + " copy")
        while FileManager.default.fileExists(atPath: dst.path) { dst = projectURL(named: base + " copy \(n)"); n += 1 }
        do { try FileManager.default.copyItem(at: url, to: dst) } catch { message = "Duplicate failed: \(error.localizedDescription)" }
        try? FileManager.default.copyItem(at: thumbnailURL(for: url), to: thumbnailURL(for: dst))
        projectListVersion += 1
    }

    func renameProject(_ url: URL, to name: String) {
        let dst = projectURL(named: name)
        guard dst != url else { return }
        if FileManager.default.fileExists(atPath: dst.path) { message = "A project named \(name) already exists."; return }
        do {
            try FileManager.default.moveItem(at: url, to: dst)
            try? FileManager.default.moveItem(at: thumbnailURL(for: url), to: thumbnailURL(for: dst))
            if documentURL == url { documentURL = dst; projectName = dst.deletingPathExtension().lastPathComponent }
        } catch { message = "Rename failed: \(error.localizedDescription)" }
        projectListVersion += 1
    }

    func deleteProject(_ url: URL) {
        try? FileManager.default.removeItem(at: url)
        try? FileManager.default.removeItem(at: thumbnailURL(for: url))
        if documentURL == url { documentURL = nil }
        projectListVersion += 1
    }

    private func writeThumbnail(for url: URL) {
        guard let image = renderer?.renderImage(width: 320, height: 240, transparent: false) else { return }
        try? FileManager.default.createDirectory(at: thumbnailsURL, withIntermediateDirectories: true)
        try? UIImage(cgImage: image).pngData()?.write(to: thumbnailURL(for: url), options: .atomic)
    }

    // MARK: - autosave and crash recovery

    /// Writes a recovery copy; never runs during a stroke or a background operation.
    func autosaveIfNeeded() {
        guard isDirty, !busy, !strokeActive else { return }
        if !write(to: autosaveURL) { message = "Autosave failed: " + lastError }
    }

    func recoverAutosave() {
        recoveryAvailable = false
        open(autosaveURL, asRecovery: true)
        if objects.isEmpty { startDefaultScene() }
    }

    func discardAutosave() {
        recoveryAvailable = false
        try? FileManager.default.removeItem(at: autosaveURL)
        startDefaultScene()
    }

    // MARK: - photo / Files reference images

    /// Imports the rendered image, not a link to the Photos database. The image
    /// survives Photos permissions changes and travels inside .sculptforge files.
    func addReferenceImage(_ data: Data, name: String) {
        guard !busy else { return }
        guard let input = UIImage(data: data), input.size.width > 0, input.size.height > 0 else {
            message = "This image could not be opened. Choose a JPG, PNG or HEIC photo."
            return
        }
        // Keep project size and UI decoding cost bounded; orientation is normalised.
        let longest = max(input.size.width, input.size.height)
        let factor = min(CGFloat(1), CGFloat(1600) / longest)
        let target = CGSize(width: max(1, input.size.width * factor), height: max(1, input.size.height * factor))
        let format = UIGraphicsImageRendererFormat.default()
        format.scale = 1
        format.opaque = false
        let normalised = UIGraphicsImageRenderer(size: target, format: format).image { _ in
            input.draw(in: CGRect(origin: .zero, size: target))
        }
        guard let bytes = normalised.jpegData(compressionQuality: 0.86) else {
            message = "Could not prepare reference image."
            return
        }
        endStroke()
        let ref = ReferenceImageAsset(name: name, imageData: bytes)
        referenceImages.append(ref)
        selectedReferenceID = ref.id
        isDirty = true
    }

    func importReferenceFile(_ url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            let data = try Data(contentsOf: url)
            addReferenceImage(data, name: url.deletingPathExtension().lastPathComponent)
        } catch {
            message = "Could not read the image: \(error.localizedDescription)"
        }
    }

    func editReference(_ id: UUID, _ change: (inout ReferenceImageAsset) -> Void) {
        guard let i = referenceImages.firstIndex(where: { $0.id == id }) else { return }
        change(&referenceImages[i])
        isDirty = true
    }

    func moveReference(_ id: UUID, byX dx: Double, byY dy: Double) {
        editReference(id) { ref in
            ref.x = min(1.25, max(-1.25, ref.x + dx))
            ref.y = min(1.25, max(-1.25, ref.y + dy))
        }
    }

    func deleteReference(_ id: UUID) {
        referenceImages.removeAll { $0.id == id }
        if selectedReferenceID == id { selectedReferenceID = referenceImages.last?.id }
        isDirty = true
    }

    // MARK: - import / export

    func importFile(_ url: URL) {
        guard !busy else { return }
        endStroke()
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        if url.pathExtension.lowercased() == SceneModel.projectExtension { open(url); documentURL = nil; isDirty = true; return }
        if ["jpg", "jpeg", "png", "heic", "heif", "webp", "tiff"].contains(url.pathExtension.lowercased()) {
            importReferenceFile(url); return
        }
        // copy into our sandbox first: the engine reads plain paths
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("import-" + url.lastPathComponent)
        try? FileManager.default.removeItem(at: tmp)
        do { try FileManager.default.copyItem(at: url, to: tmp) } catch {
            message = "Could not read \(url.lastPathComponent): \(error.localizedDescription)"
            return
        }
        defer { try? FileManager.default.removeItem(at: tmp) }
        let added = sf_import(scene, tmp.path)
        if added == 0 { message = "Import failed: " + lastError } else {
            selection = sf_object_id_at(scene, sf_object_count(scene) - 1)
            afterEdit()
            frameAll()
        }
    }

    /// Writes the export to a temporary file and returns its URL (to hand to the share sheet / Files).
    func export(format: ExportFormat, selectedOnly: Bool, fileName: String) -> URL? {
        guard !busy else { return nil }
        endStroke()
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("export", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent(SceneModel.safeFileName(fileName)).appendingPathExtension(format.fileExtension)
        let scale: Float = format.usesMillimetres ? settings.millimetresPerUnit : 1
        var ids = [selection]
        let ok: Int32 = (selectedOnly && selection != 0)
            ? sf_export(scene, url.path, format.rawValue, &ids, 1, scale)
            : sf_export(scene, url.path, format.rawValue, nil, 0, scale)
        if ok == 0 { message = "Export failed: " + lastError; return nil }
        return url
    }

    /// Renders the current view with the real renderer and writes PNG or JPEG.
    func exportImage(width: Int, height: Int, transparent: Bool, jpeg: Bool) -> URL? {
        guard !busy, let image = renderer?.renderImage(width: width, height: height, transparent: transparent && !jpeg) else {
            message = "Rendering failed (resolution too large for this device?)"
            return nil
        }
        let ui = UIImage(cgImage: image)
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("export", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent(SceneModel.safeFileName(projectName) + "-render").appendingPathExtension(jpeg ? "jpg" : "png")
        guard let data = jpeg ? ui.jpegData(compressionQuality: 0.92) : ui.pngData() else { message = "Could not encode the image."; return nil }
        do { try data.write(to: url, options: .atomic) } catch { message = "Could not write the image: \(error.localizedDescription)"; return nil }
        return url
    }
}
