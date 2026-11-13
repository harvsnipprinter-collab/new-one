// SculptForge 3D - paint, material, render, files, 3D print and settings panels
import SwiftUI
import UniformTypeIdentifiers

// MARK: - Paint

struct PaintPanel: View {
    @ObservedObject var model: SceneModel
    private let palette: [SIMD3<Float>] = [
        SIMD3<Float>(1, 1, 1), SIMD3<Float>(0.02, 0.02, 0.02), SIMD3<Float>(0.8, 0.05, 0.05), SIMD3<Float>(0.95, 0.45, 0.08),
        SIMD3<Float>(0.95, 0.8, 0.1), SIMD3<Float>(0.15, 0.6, 0.15), SIMD3<Float>(0.1, 0.35, 0.85), SIMD3<Float>(0.45, 0.15, 0.7),
        SIMD3<Float>(0.87, 0.6, 0.48), SIMD3<Float>(0.35, 0.2, 0.1), SIMD3<Float>(0.5, 0.5, 0.5), SIMD3<Float>(0.95, 0.55, 0.65)]

    var body: some View {
        PanelSection("Paint brushes") {
            ButtonGrid(columns: 3) {
                ActionButton(title: "Paint") { model.brush.type = 22; model.tool = .sculpt }
                ActionButton(title: "Smudge") { model.brush.type = 23; model.tool = .sculpt }
                ActionButton(title: "Material") { model.brush.type = 24; model.tool = .sculpt }
            }
            SliderRow(title: "Opacity (intensity)", value: model.brushValue(\.strength), range: 0...1)
            Picker("Edge", selection: $model.brush.falloff) {
                Text("Soft").tag(Int32(0)); Text("Linear").tag(Int32(2)); Text("Hard").tag(Int32(4))
            }
            .pickerStyle(.segmented)
        }
        PanelSection("Colour") {
            ColorPicker("Brush colour", selection: model.brushColorBinding, supportsOpacity: false).font(.footnote)
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 6), spacing: 6) {
                ForEach(palette.indices, id: \.self) { i in
                    Button { model.brush.color = (palette[i].x, palette[i].y, palette[i].z) } label: {
                        RoundedRectangle(cornerRadius: 6).fill(Color(linear: palette[i])).frame(height: 30)
                            .overlay(RoundedRectangle(cornerRadius: 6).stroke(Color.white.opacity(0.3)))
                    }
                    .accessibilityLabel("Palette colour \(i + 1)")
                }
            }
        }
        PanelSection("Material channels (Material brush and Fill)") {
            Toggle("Colour", isOn: model.brushFlag(\.paintColor))
            Toggle("Roughness", isOn: model.brushFlag(\.paintRough))
            SliderRow(title: "Roughness value", value: model.brushValue(\.paintRoughness), range: 0...1)
            Toggle("Metalness", isOn: model.brushFlag(\.paintMetal))
            SliderRow(title: "Metalness value", value: model.brushValue(\.paintMetalness), range: 0...1)
            ActionButton(title: "Fill unmasked surface", icon: "drop.fill") { model.fillColor() }
            Text("Painting roughness or metalness switches the object to its painted per-vertex channels.").font(.caption2).foregroundColor(.secondary)
        }
        .font(.footnote)
    }
}

// MARK: - Material

struct MaterialPanel: View {
    @ObservedObject var model: SceneModel

    private var colorBinding: Binding<Color> {
        Binding(get: {
            guard let p = model.selectedObject?.props else { return .white }
            return Color(linear: SIMD3<Float>(p.baseColor.0, p.baseColor.1, p.baseColor.2))
        }, set: { c in
            let l = c.linearRGB
            model.editProps(model.selection) { $0.baseColor = (l.x, l.y, l.z) }
        })
    }

    var body: some View {
        if let o = model.selectedObject {
            PanelSection("Material of \(o.name)") {
                ColorPicker("Base colour (tints painted colour)", selection: colorBinding, supportsOpacity: false).font(.footnote)
                SliderRow(title: "Roughness", value: model.selectedProp({ $0.roughness }, { $0.roughness = $1 }), range: 0...1, onEditing: model.editing)
                SliderRow(title: "Metallic", value: model.selectedProp({ $0.metallic }, { $0.metallic = $1 }), range: 0...1, onEditing: model.editing)
                SliderRow(title: "Opacity", value: model.selectedProp({ $0.opacity }, { $0.opacity = $1 }), range: 0.05...1, onEditing: model.editing)
                SliderRow(title: "Emission", value: model.selectedProp({ $0.emission }, { $0.emission = $1 }), range: 0...5, onEditing: model.editing)
                Toggle("Use painted roughness / metalness", isOn: Binding(get: { o.props.usePainted != 0 },
                                                                          set: { v in model.editProps(o.id) { $0.usePainted = v ? 1 : 0 } })).font(.footnote)
            }
            PanelSection("Presets") {
                ButtonGrid(columns: 2) {
                    ForEach(MaterialPreset.all) { preset in
                        Button { model.applyMaterial(preset) } label: {
                            HStack(spacing: 6) {
                                Circle().fill(Color(linear: preset.color)).frame(width: 18, height: 18)
                                Text(preset.name).font(.caption).lineLimit(1).minimumScaleFactor(0.7)
                                Spacer(minLength: 0)
                            }
                            .padding(6).background(Color.white.opacity(0.07)).cornerRadius(6)
                        }
                        .foregroundColor(.primary)
                    }
                }
            }
        } else {
            Text("Select an object first.").font(.footnote)
        }
    }
}

// MARK: - Render

struct RenderPanel: View {
    @ObservedObject var model: SceneModel
    @State private var bookmarkName = ""
    @State private var imageScale = 1
    @State private var transparent = false
    @State private var jpeg = false
    @State private var share: ShareItem?

    var body: some View {
        viewSection
        lightSection
        environmentSection
        outputSection
    }

    private var viewSection: some View {
        PanelSection("View") {
            Picker("Shading", selection: $model.render.shading) {
                ForEach(ShadingMode.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            Toggle("Orthographic camera", isOn: $model.cameraOrtho)
            SliderRow(title: "Field of view", value: $model.cameraFov, range: 10...100, decimals: 0)
            ButtonGrid(columns: 3) {
                ForEach(ViewPreset.allCases, id: \.self) { v in ActionButton(title: v.rawValue) { model.camera.setView(v) } }
                ActionButton(title: "Frame all") { model.frameAll() }
                ActionButton(title: "Focus") { model.frameSelection() }
                ActionButton(title: "Reset") { let o = model.cameraOrtho; model.camera = OrbitCamera(); model.cameraOrtho = o; model.camera.fovDegrees = model.cameraFov }
            }
            Group {
                Toggle("Floor grid and axes", isOn: $model.render.showGrid)
                Toggle("Wireframe overlay", isOn: $model.render.showWireframe)
                Toggle("Mask overlay", isOn: $model.render.showMask)
                Toggle("Brush cursor", isOn: $model.render.showBrushCursor)
            }
            HStack {
                TextField("Bookmark name", text: $bookmarkName).textFieldStyle(.roundedBorder)
                ActionButton(title: "Save view") {
                    let c = model.camera
                    model.bookmarks.append(CameraBookmark(name: bookmarkName.isEmpty ? "View \(model.bookmarks.count + 1)" : bookmarkName,
                                                          target: c.target, distance: c.distance, yaw: c.yaw, pitch: c.pitch))
                    bookmarkName = ""
                    model.isDirty = true
                }
            }
            ForEach(model.bookmarks) { b in
                HStack {
                    Button(b.name) { model.camera.target = b.target; model.camera.distance = b.distance; model.camera.yaw = b.yaw; model.camera.pitch = b.pitch }
                    Spacer()
                    Button(role: .destructive) { model.bookmarks.removeAll { $0.id == b.id } } label: { Image(systemName: "trash") }
                }
            }
        }
        .font(.footnote)
    }

    private var lightSection: some View {
        PanelSection("Lights") {
            ButtonGrid(columns: 3) {
                ActionButton(title: "Studio") { model.render.lights = RenderSettings.studioLights }
                ActionButton(title: "Rim") { model.render.lights = RenderSettings.rimLights }
                ActionButton(title: "Soft") { model.render.lights = RenderSettings.softLights }
            }
            ForEach($model.render.lights) { $light in
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Toggle("On", isOn: $light.enabled).toggleStyle(.button)
                        Toggle("Point", isOn: $light.isPoint).toggleStyle(.button)
                        ColorPicker("", selection: Binding(get: { Color(linear: light.color) }, set: { light.color = $0.linearRGB }), supportsOpacity: false)
                    }
                    SliderRow(title: "Direction (around)", value: $light.azimuth, range: -3.14...3.14)
                    SliderRow(title: "Height", value: $light.elevation, range: -1.5...1.5)
                    SliderRow(title: "Intensity", value: $light.intensity, range: 0...10)
                    if light.isPoint { SliderRow(title: "Distance", value: $light.distance, range: 0.5...30) }
                }
                .padding(6).background(Color.white.opacity(0.05)).cornerRadius(8)
            }
            if model.render.lights.count < 4 {
                ActionButton(title: "Add light", icon: "plus") { model.render.lights.append(LightSetting()) }
            }
            if model.render.lights.count > 1 {
                ActionButton(title: "Remove last light", destructive: true) { model.render.lights.removeLast() }
            }
        }
        .font(.footnote)
    }

    private var environmentSection: some View {
        PanelSection("Environment and tone") {
            SliderRow(title: "Exposure", value: $model.render.exposure, range: 0.1...4)
            SliderRow(title: "Ambient light", value: $model.render.ambientIntensity, range: 0...3)
            ColorPicker("Sky colour", selection: Binding(get: { Color(linear: model.render.ambientSky) }, set: { model.render.ambientSky = $0.linearRGB }), supportsOpacity: false)
            ColorPicker("Ground colour", selection: Binding(get: { Color(linear: model.render.ambientGround) }, set: { model.render.ambientGround = $0.linearRGB }), supportsOpacity: false)
            ColorPicker("Background top", selection: Binding(get: { Color(linear: model.render.backgroundTop) }, set: { model.render.backgroundTop = $0.linearRGB }), supportsOpacity: false)
            ColorPicker("Background bottom", selection: Binding(get: { Color(linear: model.render.backgroundBottom) }, set: { model.render.backgroundBottom = $0.linearRGB }), supportsOpacity: false)
        }
        .font(.footnote)
    }

    private var outputSection: some View {
        PanelSection("Render image") {
            Picker("Size", selection: $imageScale) { Text("1280").tag(1); Text("2560").tag(2); Text("3840").tag(3) }.pickerStyle(.segmented)
            Toggle("Transparent background (PNG)", isOn: $transparent).disabled(jpeg)
            Toggle("JPEG instead of PNG", isOn: $jpeg)
            ActionButton(title: "Render and share", icon: "photo") {
                let w = 1280 * imageScale
                let h = Int(Float(w) / max(model.camera.aspect, 0.2))
                if let url = model.exportImage(width: w, height: min(h, 8192), transparent: transparent, jpeg: jpeg) { share = ShareItem(url: url) }
            }
            Text("Renders the current view with the live renderer (lights, materials, 4x MSAA) without grid, cursor or wireframe.")
                .font(.caption2).foregroundColor(.secondary)
        }
        .font(.footnote)
        .sheet(item: $share) { item in ActivityView(items: [item.url]) }
    }
}

// MARK: - Files

struct FilesPanel: View {
    @ObservedObject var model: SceneModel
    @State private var name = ""
    @State private var importing = false
    @State private var format: ExportFormat = .stl
    @State private var selectedOnly = false
    @State private var share: ShareItem?
    @State private var renameTarget: ProjectEntry?
    @State private var renameText = ""

    var body: some View {
        PanelSection("Project: \(model.projectName)") {
            ButtonGrid(columns: 3) {
                ActionButton(title: "New") { model.newProject() }
                ActionButton(title: "Save") { model.save() }
                ActionButton(title: "Share file") {
                    if model.save(), let url = model.documentURL { share = ShareItem(url: url) }
                }
            }
            HStack {
                TextField("Save as name", text: $name).textFieldStyle(.roundedBorder)
                ActionButton(title: "Save as") { if !name.isEmpty { model.saveAs(name: name); name = "" } }
            }
            Text("Projects are stored in the app's Documents folder (visible in the Files app) and autosaved for crash recovery.")
                .font(.caption2).foregroundColor(.secondary)
        }
        PanelSection("Saved projects") {
            let entries = model.projectList()
            if entries.isEmpty { Text("No saved projects yet.").font(.footnote).foregroundColor(.secondary) }
            ForEach(entries) { e in
                HStack(spacing: 8) {
                    thumbnail(e)
                    VStack(alignment: .leading, spacing: 0) {
                        Text(e.name).font(.footnote.weight(.semibold)).lineLimit(1)
                        Text("\(e.modified.formatted(date: .abbreviated, time: .shortened)) · \(e.sizeBytes / 1024) KB").font(.caption2).foregroundColor(.secondary)
                    }
                    Spacer(minLength: 0)
                    Menu {
                        Button("Open") { model.open(e.url) }
                        Button("Duplicate") { model.duplicateProject(e.url) }
                        Button("Rename") { renameText = e.name; renameTarget = e }
                        Button("Delete", role: .destructive) { model.deleteProject(e.url) }
                    } label: { Image(systemName: "ellipsis.circle").font(.title3) }
                }
                .contentShape(Rectangle())
                .onTapGesture { model.open(e.url) }
            }
        }
        .id(model.projectListVersion)
        .alert("Rename project", isPresented: Binding(get: { renameTarget != nil }, set: { if !$0 { renameTarget = nil } })) {
            TextField("Name", text: $renameText)
            Button("Rename") { if let t = renameTarget, !renameText.isEmpty { model.renameProject(t.url, to: renameText) }; renameTarget = nil }
            Button("Cancel", role: .cancel) { renameTarget = nil }
        }
        PanelSection("Import") {
            ActionButton(title: "Import STL / OBJ / PLY / GLB…", icon: "square.and.arrow.down.on.square") { importing = true }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            if case .success(let urls) = result, let url = urls.first { model.importFile(url) }
            if case .failure(let error) = result { model.message = "Import failed: \(error.localizedDescription)" }
        }
        PanelSection("Export model") {
            Picker("Format", selection: $format) { ForEach(ExportFormat.allCases) { Text($0.title).tag($0) } }.pickerStyle(.segmented)
            Toggle("Selected object only", isOn: $selectedOnly).font(.footnote)
            ActionButton(title: "Export and share / save to Files", icon: "square.and.arrow.up") {
                if let url = model.export(format: format, selectedOnly: selectedOnly, fileName: model.projectName) { share = ShareItem(url: url) }
            }
            Text(format.usesMillimetres ? "Written in millimetres using the scale from the 3D Print panel (\(String(format: "%.1f", Double(model.settings.millimetresPerUnit))) mm per unit)."
                                        : "Written in scene units. OBJ, PLY and GLB keep vertex colours; GLB also keeps objects, transforms and materials.")
                .font(.caption2).foregroundColor(.secondary)
        }
        .sheet(item: $share) { item in ActivityView(items: [item.url]) }
    }

    @ViewBuilder
    private func thumbnail(_ e: ProjectEntry) -> some View {
        if let image = UIImage(contentsOfFile: model.thumbnailURL(for: e.url).path) {
            Image(uiImage: image).resizable().aspectRatio(contentMode: .fill).frame(width: 56, height: 42).clipped().cornerRadius(6)
        } else {
            RoundedRectangle(cornerRadius: 6).fill(Color.white.opacity(0.08)).frame(width: 56, height: 42)
                .overlay(Image(systemName: "cube").foregroundColor(.secondary))
        }
    }
}

// MARK: - 3D print

struct PrintPanel: View {
    @ObservedObject var model: SceneModel
    @State private var analysis: SFAnalysis?
    @State private var selectedOnly = false
    @State private var thinMm: Float = 0.8
    @State private var targetHeightMm: Float = 100
    @State private var voxelMm: Float = 0.6
    @State private var share: ShareItem?

    var body: some View {
        PanelSection("Scale and units") {
            SliderRow(title: "Millimetres per scene unit", value: $model.settings.millimetresPerUnit, range: 1...200, decimals: 1)
            SliderRow(title: "Target height (mm)", value: $targetHeightMm, range: 10...400, decimals: 0)
            ActionButton(title: "Scale export so the model is \(Int(targetHeightMm)) mm tall") {
                // measure in scene units, then choose the unit scale that gives the requested height
                var a = SFAnalysis()
                if sf_analyze(model.scene, nil, 0, 1, 0, &a) != 0 {
                    let h = a.boundsMax.1 - a.boundsMin.1
                    if h > 1e-6 { model.settings.millimetresPerUnit = max(0.01, targetHeightMm / h) }
                    analysis = model.analyze(selectedOnly: selectedOnly, thinThresholdMm: thinMm)
                } else { model.message = model.lastError }
            }
        }
        PanelSection("Check geometry") {
            Toggle("Selected object only", isOn: $selectedOnly).font(.footnote)
            SliderRow(title: "Thin-wall warning below (mm)", value: $thinMm, range: 0.2...5, decimals: 1)
            ActionButton(title: "Analyse", icon: "checkmark.shield") { analysis = model.analyze(selectedOnly: selectedOnly, thinThresholdMm: thinMm) }
            if let a = analysis { report(a) }
        }
        PanelSection("Make printable") {
            SliderRow(title: "Voxel size (mm)", value: $voxelMm, range: 0.1...5, decimals: 2)
            ActionButton(title: "Merge visible objects into one solid", icon: "cube.fill") {
                let world = voxelMm / max(model.settings.millimetresPerUnit, 0.001)
                model.makePrintable(voxelSize: world) { ok in
                    if ok { analysis = model.analyze(selectedOnly: true, thinThresholdMm: thinMm) }
                }
            }
            Text("Joins every visible object and rebuilds one closed surface. Undo restores the separate objects. Save your project first: this replaces the editable parts in the scene.")
                .font(.caption2).foregroundColor(.secondary)
        }
        PanelSection("Export for slicer") {
            ButtonGrid(columns: 2) {
                ActionButton(title: "STL", icon: "square.and.arrow.up") {
                    if let url = model.export(format: .stl, selectedOnly: selectedOnly, fileName: model.projectName) { share = ShareItem(url: url) }
                }
                ActionButton(title: "3MF", icon: "square.and.arrow.up") {
                    if let url = model.export(format: .threeMF, selectedOnly: selectedOnly, fileName: model.projectName) { share = ShareItem(url: url) }
                }
            }
            Text("A file that exports is not automatically printable. Run Analyse and resolve any issue listed above first.")
                .font(.caption2).foregroundColor(.secondary)
        }
        .sheet(item: $share) { item in ActivityView(items: [item.url]) }
    }

    private func line(_ ok: Bool, _ text: String) -> some View {
        Label(text, systemImage: ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
            .foregroundColor(ok ? .green : .orange).font(.caption)
    }

    @ViewBuilder
    private func report(_ a: SFAnalysis) -> some View {
        let sx = a.boundsMax.0 - a.boundsMin.0, sy = a.boundsMax.1 - a.boundsMin.1, sz = a.boundsMax.2 - a.boundsMin.2
        VStack(alignment: .leading, spacing: 3) {
            Text(String(format: "Size: %.1f x %.1f x %.1f mm", Double(sx), Double(sy), Double(sz))).font(.caption.monospacedDigit())
            Text(String(format: "Volume: %.2f cm³   Area: %.1f cm²", abs(a.volume) / 1000, a.surfaceArea / 100)).font(.caption.monospacedDigit())
            Text("\(a.triangles) triangles, \(a.components) separate part(s)").font(.caption)
            line(a.boundaryEdges == 0, a.boundaryEdges == 0 ? "No open boundaries" : "\(a.boundaryEdges) open edges in \(a.holes) hole(s)")
            line(a.nonManifoldEdges == 0, a.nonManifoldEdges == 0 ? "No non-manifold edges" : "\(a.nonManifoldEdges) non-manifold edges")
            line(a.inconsistentEdges == 0 && a.invertedNormals == 0, a.invertedNormals != 0 ? "Normals point inward (flip them)" :
                    (a.inconsistentEdges == 0 ? "Consistent face orientation" : "\(a.inconsistentEdges) edges with inconsistent orientation"))
            line(a.degenerateTriangles == 0, a.degenerateTriangles == 0 ? "No zero-area triangles" : "\(a.degenerateTriangles) zero-area triangles")
            line(a.components == 1, a.components == 1 ? "Single connected part" : "\(a.components) disconnected parts (overlapping shells print unreliably)")
            if a.minThickness >= 0 {
                line(a.thinSamples == 0, a.thinSamples == 0 ? String(format: "Thinnest sampled wall %.2f mm", Double(a.minThickness))
                        : "\(a.thinSamples) of \(a.thicknessSamples) samples thinner than the limit (min " + String(format: "%.2f", Double(a.minThickness)) + " mm)")
            }
            line(a.watertight != 0 && a.components == 1 && a.invertedNormals == 0,
                 a.watertight != 0 && a.components == 1 && a.invertedNormals == 0 ? "Watertight single solid" : "Not a single watertight solid yet")
        }
    }
}

// MARK: - Settings

struct SettingsPanel: View {
    @ObservedObject var model: SceneModel

    var body: some View {
        PanelSection("Input") {
            Toggle("Fingers can sculpt (off: Pencil only)", isOn: $model.settings.fingerSculpts)
            Toggle("Left-handed layout", isOn: $model.settings.leftHanded)
            SliderRow(title: "Pressure response (lower = softer)", value: $model.settings.pressureGamma, range: 0.3...3)
            Text("One finger on the object sculpts, one finger on empty space orbits, two fingers pan / zoom / turn. A second finger within 0.3 s cancels an accidental stroke.")
                .font(.caption2).foregroundColor(.secondary)
        }
        .font(.footnote)
        PanelSection("History and autosave") {
            Stepper("Undo steps: \(model.settings.historySteps)", value: $model.settings.historySteps, in: 5...200, step: 5)
            Stepper("Autosave every \(Int(model.settings.autosaveSeconds)) s", value: $model.settings.autosaveSeconds, in: 15...600, step: 15)
            Text("Undo memory is capped at 768 MB; the oldest steps are dropped first.").font(.caption2).foregroundColor(.secondary)
        }
        .font(.footnote)
        PanelSection("Status") {
            Text("Scene: \(model.totalTriangles.formatted()) triangles in \(model.objects.count) objects").font(.caption)
            Text("Engine memory incl. undo: \(Int(model.memoryMB)) MB").font(.caption)
            Text("SculptForge 3D \(Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "")").font(.caption2).foregroundColor(.secondary)
        }
    }
}
