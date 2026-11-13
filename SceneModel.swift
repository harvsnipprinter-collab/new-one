// SculptForge 3D - Swift owner of the C++ engine scene. All engine calls go through this class.
// Threading: the engine is used from the main thread, except during `runHeavy`, when a single
// background task owns it and `busy` is true (UI, input and renderer stay away from the engine).
import Combine
import Foundation
import SwiftUI
import simd

struct ObjectInfo: Identifiable {
    let id: UInt32
    var name: String
    var props: SFObjectProps
    var world: float4x4
    var vertexCount: Int
    var triangleCount: Int
}

struct BrushCursor {
    var point: SIMD3<Float>
    var normal: SIMD3<Float>
}

// Progress / cancel hooks for long engine operations (plain globals so the callback can be a C function).
var gOperationProgress: Float = 0
var gCancelRequested = false
let engineProgressCallback: SFProgress = { fraction, _ in
    gOperationProgress = fraction
    return gCancelRequested ? 0 : 1
}

final class SceneModel: ObservableObject {
    let scene: OpaquePointer

    @Published var objects: [ObjectInfo] = []
    @Published var selection: UInt32 = 0
    @Published var multiSelection: Set<UInt32> = []
    @Published var brush = SFBrush()
    @Published var tool: Tool = .sculpt
    @Published var transformMode: TransformMode = .move
    /// When a partial mask is present, edit the unmasked vertices instead of moving the whole object.
    @Published var maskOverlayOpacity: Float = 0.78
    @Published var useMaskedTransform = true
    @Published var maskPivotWorld: SIMD3<Float>? = nil
    private(set) var maskedGestureActive = false
    @Published var render = RenderSettings()
    @Published var settings = AppSettings() { didSet { saveSettings() } }
    @Published var busy = false
    @Published var busyTitle = ""
    @Published var message: String?
    @Published var canUndo = false
    @Published var canRedo = false
    @Published var projectName = "Untitled"
    @Published var brushPresets: [BrushPreset] = [] { didSet { savePresets() } }
    @Published var bookmarks: [CameraBookmark] = []
    @Published var referenceImages: [ReferenceImageAsset] = []
    @Published var selectedReferenceID: UUID? = nil
    @Published var cameraOrtho = false { didSet { camera.orthographic = cameraOrtho } }
    @Published var cameraFov: Float = 40 { didSet { camera.fovDegrees = cameraFov } }
    @Published var totalTriangles: Int = 0
    @Published var memoryMB: Double = 0
    @Published var recoveryAvailable = false
    @Published var projectListVersion = 0

    // Not published: read by the renderer every frame.
    var camera = OrbitCamera()
    var cursor: BrushCursor?
    var fullUpload = Set<UInt32>()
    var dirtyRanges: [UInt32: (UInt32, UInt32)] = [:]
    var frameSeconds: Double = 0
    var isDirty = false
    var strokeActive = false
    weak var renderer: Renderer?
    var documentURL: URL?
    private var autosaveTimer: Timer?

    init() {
        scene = sf_scene_create()
        sf_brush_default(&brush)
        loadSettings()
        loadPresets()
        sf_history_set_limits(scene, UInt32(max(1, settings.historySteps)), 768 << 20)
        recoveryAvailable = FileManager.default.fileExists(atPath: autosaveURL.path)
        if !recoveryAvailable { startDefaultScene() }
        scheduleAutosave()
    }

    deinit {
        autosaveTimer?.invalidate()
        sf_scene_destroy(scene)
    }

    var lastError: String { String(cString: sf_last_error(scene)) }
    var selectedObject: ObjectInfo? { objects.first { $0.id == selection } }

    func startDefaultScene() {
        sf_scene_clear(scene)
        let params: [Float] = [1, 6] // icosphere, 81,920 triangles
        let id = sf_add_primitive(scene, 1, params, Int32(params.count))
        sf_history_clear(scene)
        selection = id
        projectName = "Untitled"
        referenceImages = []
        selectedReferenceID = nil
        documentURL = nil
        isDirty = false
        camera = OrbitCamera()
        camera.orthographic = cameraOrtho
        camera.fovDegrees = cameraFov
        markAllForUpload()
        refresh()
    }

    // MARK: - state mirroring

    /// Re-read the object list and counters from the engine (cheap; no geometry is copied).
    func refresh() {
        guard !busy else { return }
        var list: [ObjectInfo] = []
        let count = sf_object_count(scene)
        var i: Int32 = 0
        while i < count {
            let id = sf_object_id_at(scene, i)
            var p = SFObjectProps()
            sf_object_get_props(scene, id, &p)
            var m = [Float](repeating: 0, count: 16)
            sf_object_world_matrix(scene, id, &m)
            var v = SFMeshView()
            sf_object_mesh(scene, id, &v)
            let world = float4x4(columns: (SIMD4<Float>(m[0], m[1], m[2], m[3]), SIMD4<Float>(m[4], m[5], m[6], m[7]),
                                           SIMD4<Float>(m[8], m[9], m[10], m[11]), SIMD4<Float>(m[12], m[13], m[14], m[15])))
            list.append(ObjectInfo(id: id, name: String(cString: sf_object_name(scene, id)), props: p, world: world,
                                   vertexCount: Int(v.vertexCount), triangleCount: Int(v.triangleCount)))
            i += 1
        }
        objects = list
        if selection != 0 && !busy {
            var pivot = [Float](repeating: 0, count: 3)
            maskPivotWorld = sf_mask_edit_pivot(scene, selection, &pivot) != 0
                ? SIMD3<Float>(pivot[0], pivot[1], pivot[2]) : nil
        } else { maskPivotWorld = nil }
        if !list.contains(where: { $0.id == selection }) { selection = list.last?.id ?? 0 }
        multiSelection = multiSelection.filter { id in list.contains { $0.id == id } }
        canUndo = sf_can_undo(scene) != 0
        canRedo = sf_can_redo(scene) != 0
        totalTriangles = Int(sf_scene_triangle_count(scene))
        memoryMB = Double(sf_scene_memory_bytes(scene)) / 1_048_576
    }

    func markAllForUpload() {
        let count = sf_object_count(scene)
        var i: Int32 = 0
        while i < count { fullUpload.insert(sf_object_id_at(scene, i)); i += 1 }
    }

    /// Call after any engine edit that is not a brush stroke.
    func afterEdit() {
        isDirty = true
        markAllForUpload()
        refresh()
    }

    /// Report an engine result: shows the engine's error text on failure.
    @discardableResult
    func finish(_ result: Int32) -> Bool {
        if result == 0 { message = lastError }
        afterEdit()
        return result != 0
    }

    // MARK: - history

    func undo() {
        guard !busy else { return }
        endStroke()
        if sf_undo(scene) != 0 { afterEdit() }
    }

    func redo() {
        guard !busy else { return }
        endStroke()
        if sf_redo(scene) != 0 { afterEdit() }
    }

    // MARK: - strokes (called by the viewport input handler)

    private func mappedPressure(_ p: Float) -> Float {
        pow(max(0.02, min(1, p)), max(0.2, settings.pressureGamma))
    }

    func beginStroke(origin: SIMD3<Float>, direction: SIMD3<Float>, pressure: Float) -> Bool {
        guard !busy, selection != 0 else { return false }
        var b = brush
        let o: [Float] = [origin.x, origin.y, origin.z], d: [Float] = [direction.x, direction.y, direction.z]
        strokeActive = sf_stroke_begin(scene, selection, &b, o, d, mappedPressure(pressure)) != 0
        if strokeActive {
            collectDirty()
            // the Material brush may switch the object to painted channels: refresh cached material state
            if b.type == 24 { fullUpload.insert(selection); refresh() }
        }
        return strokeActive
    }

    func moveStroke(origin: SIMD3<Float>, direction: SIMD3<Float>, pressure: Float) {
        guard strokeActive else { return }
        let o: [Float] = [origin.x, origin.y, origin.z], d: [Float] = [direction.x, direction.y, direction.z]
        sf_stroke_move(scene, o, d, mappedPressure(pressure))
        collectDirty()
    }

    func endStroke() {
        guard strokeActive else { return }
        strokeActive = false
        let changed = sf_stroke_end(scene) != 0
        if changed { isDirty = true }
        fullUpload.insert(selection)
        refresh()
    }

    /// Abort the stroke in progress and remove whatever it did (used when a second finger lands).
    func cancelStroke() {
        guard strokeActive else { return }
        strokeActive = false
        if sf_stroke_end(scene) != 0 { sf_undo(scene) }
        fullUpload.insert(selection)
        refresh()
    }

    private func collectDirty() {
        if brush.dyntopo != 0 { fullUpload.insert(selection); return } // vertex/index counts may have changed
        var mn: UInt32 = 0, mx: UInt32 = 0
        guard sf_stroke_take_dirty(scene, &mn, &mx) != 0 else { return }
        if let old = dirtyRanges[selection] { dirtyRanges[selection] = (min(old.0, mn), max(old.1, mx)) }
        else { dirtyRanges[selection] = (mn, mx) }
    }

    /// World-space ray pick. Returns the object id (0 when nothing is hit) and updates nothing.
    func pick(origin: SIMD3<Float>, direction: SIMD3<Float>) -> (id: UInt32, point: SIMD3<Float>, normal: SIMD3<Float>)? {
        guard !busy else { return nil }
        let o: [Float] = [origin.x, origin.y, origin.z], d: [Float] = [direction.x, direction.y, direction.z]
        var id: UInt32 = 0
        var p = [Float](repeating: 0, count: 3), n = [Float](repeating: 0, count: 3)
        guard sf_pick(scene, o, d, &id, &p, &n) != 0 else { return nil }
        return (id, SIMD3<Float>(p[0], p[1], p[2]), SIMD3<Float>(n[0], n[1], n[2]))
    }

    // MARK: - object properties

    func props(of id: UInt32) -> SFObjectProps? {
        var p = SFObjectProps()
        return sf_object_get_props(scene, id, &p) != 0 ? p : nil
    }

    /// Apply a property change immediately without creating an undo step (use inside begin/end).
    func setProps(_ id: UInt32, _ change: (inout SFObjectProps) -> Void) {
        guard !busy, var p = props(of: id) else { return }
        change(&p)
        if sf_object_set_props(scene, id, &p) == 0 { message = lastError }
        isDirty = true
        refresh()
    }

    func beginPropsEdit(_ id: UInt32) { if !busy { sf_object_begin_edit(scene, id) } }
    func endPropsEdit(_ id: UInt32) { if !busy { sf_object_end_edit(scene, id); refresh() } }

    /// One-shot undoable property change.
    func editProps(_ id: UInt32, _ change: (inout SFObjectProps) -> Void) {
        beginPropsEdit(id)
        setProps(id, change)
        endPropsEdit(id)
    }

    // MARK: - interactive object / mask-aware transforms

    func beginTransformGesture() {
        guard selection != 0, !busy else { return }
        maskedGestureActive = false
        if useMaskedTransform && maskPivotWorld != nil {
            var pivot = [Float](repeating: 0, count: 3)
            if sf_masked_transform_begin(scene, selection, &pivot) != 0 {
                maskedGestureActive = true
                maskPivotWorld = SIMD3<Float>(pivot[0], pivot[1], pivot[2])
                return
            }
        }
        beginPropsEdit(selection)
    }

    func updateMaskedTransform(translation: SIMD3<Float> = .zero,
                               axis: SIMD3<Float> = SIMD3<Float>(0, 1, 0),
                               angle: Float = 0, scaling: SIMD3<Float> = SIMD3<Float>(repeating: 1)) {
        guard maskedGestureActive, !busy else { return }
        let t: [Float] = [translation.x, translation.y, translation.z]
        let a: [Float] = [axis.x, axis.y, axis.z]
        let s: [Float] = [scaling.x, scaling.y, scaling.z]
        if sf_masked_transform_update(scene, t, a, angle, s) != 0 {
            fullUpload.insert(selection)
            isDirty = true
            refresh()
        } else { message = lastError }
    }

    func endTransformGesture(cancelled: Bool = false) {
        if maskedGestureActive {
            sf_masked_transform_end(scene, cancelled ? 0 : 1)
            maskedGestureActive = false
            afterEdit()
        } else {
            endPropsEdit(selection)
        }
    }

    // MARK: - long operations

    /// Run an engine operation off the main thread with a progress overlay.
    func runHeavy(_ title: String, _ work: @escaping (OpaquePointer) -> Int32, completion: ((Bool) -> Void)? = nil) {
        guard !busy else { return }
        endStroke()
        busy = true
        busyTitle = title
        gCancelRequested = false
        gOperationProgress = 0
        let scene = self.scene
        DispatchQueue.global(qos: .userInitiated).async {
            let result = work(scene)
            let error = result == 0 ? String(cString: sf_last_error(scene)) : ""
            DispatchQueue.main.async {
                self.busy = false
                if result == 0 { self.message = error }
                self.afterEdit()
                completion?(result != 0)
            }
        }
    }

    // MARK: - settings persistence

    private func loadSettings() {
        if let data = UserDefaults.standard.data(forKey: "settings"),
           let s = try? JSONDecoder().decode(AppSettings.self, from: data) {
            settings = s
        }
    }
    private func saveSettings() {
        if let data = try? JSONEncoder().encode(settings) { UserDefaults.standard.set(data, forKey: "settings") }
        sf_history_set_limits(scene, UInt32(max(1, settings.historySteps)), 768 << 20)
        scheduleAutosave()
    }
    private func loadPresets() {
        if let data = UserDefaults.standard.data(forKey: "brushPresets"),
           let p = try? JSONDecoder().decode([BrushPreset].self, from: data) {
            brushPresets = p
        }
    }
    private func savePresets() {
        if let data = try? JSONEncoder().encode(brushPresets) { UserDefaults.standard.set(data, forKey: "brushPresets") }
    }

    func scheduleAutosave() {
        autosaveTimer?.invalidate()
        let interval = max(10, settings.autosaveSeconds)
        autosaveTimer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { [weak self] _ in self?.autosaveIfNeeded() }
    }
}
