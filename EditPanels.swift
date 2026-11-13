// SculptForge 3D - brush, objects, topology and mask panels. Every control calls a real engine function.
import SwiftUI

// MARK: - Brush

struct BrushPanel: View {
    @ObservedObject var model: SceneModel
    @State private var search = ""
    @State private var presetName = ""

    private var filtered: [Int32] {
        (0..<sf_brush_count()).filter { search.isEmpty || BrushIcons.title($0).localizedCaseInsensitiveContains(search) }
    }

    var body: some View {
        PanelSection("Tool search") {
            TextField("Search brushes", text: $search).textFieldStyle(.roundedBorder)
            ButtonGrid(columns: 3) {
                ForEach(filtered, id: \.self) { t in
                    Button { model.brush.type = t; model.tool = .sculpt } label: {
                        Label(BrushIcons.title(t), systemImage: BrushIcons.icon(t)).font(.caption2).lineLimit(1).minimumScaleFactor(0.6)
                            .frame(maxWidth: .infinity).padding(.vertical, 5)
                            .background(model.brush.type == t ? Color.orange.opacity(0.8) : Color.white.opacity(0.07)).cornerRadius(6)
                    }
                    .foregroundColor(model.brush.type == t ? .black : .primary)
                }
            }
            Text(BrushIcons.hint(model.brush.type)).font(.caption2).foregroundColor(.secondary)
        }
        PanelSection("Stroke") {
            SliderRow(title: "Radius", value: model.brushValue(\.radius), range: 0.01...1.5)
            SliderRow(title: "Intensity", value: model.brushValue(\.strength), range: 0...1)
            SliderRow(title: "Spacing (x radius)", value: model.brushValue(\.spacing), range: 0.03...1)
            SliderRow(title: "Lazy stroke smoothing", value: model.brushValue(\.smoothing), range: 0...0.95)
            Picker("Falloff", selection: $model.brush.falloff) {
                Text("Smooth").tag(Int32(0)); Text("Sphere").tag(Int32(1)); Text("Linear").tag(Int32(2))
                Text("Sharp").tag(Int32(3)); Text("Constant").tag(Int32(4))
            }
            .pickerStyle(.segmented)
            Group {
                Toggle("Invert", isOn: model.brushFlag(\.invert))
                Toggle("Front faces only", isOn: model.brushFlag(\.frontFacesOnly))
                Toggle("Pressure controls intensity", isOn: model.brushFlag(\.pressureStrength))
                Toggle("Pressure controls radius", isOn: model.brushFlag(\.pressureRadius))
            }
            if model.brush.type == 16 { SliderRow(title: "Layer height", value: model.brushValue(\.layerHeight), range: 0.005...0.5, decimals: 3) }
            if model.brush.type == 21 {
                Stepper("Face group \(model.brush.faceGroup)", value: Binding(get: { Int(model.brush.faceGroup) }, set: { model.brush.faceGroup = UInt32(max(0, $0)) }), in: 0...255)
            }
        }
        .font(.footnote)
        PanelSection("Symmetry") {
            HStack {
                Toggle("X", isOn: model.brushFlag(\.symX)).toggleStyle(.button)
                Toggle("Y", isOn: model.brushFlag(\.symY)).toggleStyle(.button)
                Toggle("Z", isOn: model.brushFlag(\.symZ)).toggleStyle(.button)
                Spacer()
                Text("object space").font(.caption2).foregroundColor(.secondary)
            }
            Stepper("Radial copies: \(model.brush.radialCount)", value: Binding(get: { Int(model.brush.radialCount) }, set: { model.brush.radialCount = Int32($0) }), in: 1...32)
            Picker("Radial axis", selection: $model.brush.radialAxis) {
                Text("X").tag(Int32(0)); Text("Y").tag(Int32(1)); Text("Z").tag(Int32(2))
            }
            .pickerStyle(.segmented)
        }
        .font(.footnote)
        PanelSection("Dynamic topology") {
            Toggle("Enabled while sculpting", isOn: model.brushFlag(\.dyntopo))
            Picker("Mode", selection: $model.brush.dyntopoMode) {
                Text("Subdivide").tag(Int32(0)); Text("Collapse").tag(Int32(1)); Text("Both").tag(Int32(2))
            }
            .pickerStyle(.segmented)
            SliderRow(title: "Detail (edge length)", value: model.brushValue(\.dyntopoDetail), range: 0.005...0.3, decimals: 3)
            Text("Works with surface brushes. Grab brushes and Layer keep the topology fixed.").font(.caption2).foregroundColor(.secondary)
        }
        .font(.footnote)
        PanelSection("Presets") {
            HStack {
                TextField("Preset name", text: $presetName).textFieldStyle(.roundedBorder)
                ActionButton(title: "Save") { model.savePreset(named: presetName); presetName = "" }
            }
            ForEach(model.brushPresets) { p in
                HStack {
                    Button { model.applyPreset(p); model.tool = .sculpt } label: {
                        Label(p.name, systemImage: BrushIcons.icon(p.type)).font(.footnote)
                    }
                    Spacer()
                    Button(role: .destructive) { model.brushPresets.removeAll { $0.id == p.id } } label: { Image(systemName: "trash") }
                }
            }
            ActionButton(title: "Reset brush to defaults", icon: "arrow.counterclockwise") { model.resetBrush() }
        }
    }
}

// MARK: - Objects

struct ScenePanel: View {
    @ObservedObject var model: SceneModel
    @State private var primitiveType: Int32 = 1
    @State private var params: [PrimitiveParam] = SceneModel.primitiveParams(1)
    @State private var newName = ""

    var body: some View {
        addSection
        listSection
        if let o = model.selectedObject { selectedSection(o) }
    }

    private var addSection: some View {
        PanelSection("Add primitive") {
            Picker("Shape", selection: $primitiveType) {
                ForEach(0..<sf_primitive_count(), id: \.self) { t in Text(String(cString: sf_primitive_name(t))).tag(t) }
            }
            .onChange(of: primitiveType) { t in params = SceneModel.primitiveParams(t) }
            ForEach(params.indices, id: \.self) { i in
                SliderRow(title: params[i].name,
                          value: Binding(get: { i < params.count ? params[i].value : 0 },
                                         set: { if i < params.count { params[i].value = params[i].isInteger ? $0.rounded() : $0 } }),
                          range: params[i].range, decimals: params[i].isInteger ? 0 : 2)
            }
            ActionButton(title: "Add to scene", icon: "plus") { model.addPrimitive(type: primitiveType, params: params.map { $0.value }) }
        }
    }

    private var listSection: some View {
        PanelSection("Objects (\(model.objects.count))") {
            ForEach(model.objects) { o in
                HStack(spacing: 6) {
                    Button {
                        if model.multiSelection.contains(o.id) { model.multiSelection.remove(o.id) } else { model.multiSelection.insert(o.id) }
                    } label: { Image(systemName: model.multiSelection.contains(o.id) ? "checkmark.square.fill" : "square") }
                        .accessibilityLabel("Tick \(o.name) for merge")
                    Button { model.selection = o.id } label: {
                        VStack(alignment: .leading, spacing: 0) {
                            Text((o.props.parent != 0 ? "↳ " : "") + o.name).font(.footnote.weight(o.id == model.selection ? .bold : .regular)).lineLimit(1)
                            Text("\(o.triangleCount.formatted()) tris").font(.caption2).foregroundColor(.secondary)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .foregroundColor(o.id == model.selection ? .orange : .primary)
                    Button { model.toggleVisible(o.id) } label: { Image(systemName: o.props.visible != 0 ? "eye" : "eye.slash") }
                        .accessibilityLabel(o.props.visible != 0 ? "Hide" : "Show")
                    Button { model.toggleLocked(o.id) } label: { Image(systemName: o.props.locked != 0 ? "lock.fill" : "lock.open") }
                        .accessibilityLabel(o.props.locked != 0 ? "Unlock" : "Lock")
                }
            }
            ButtonGrid(columns: 2) {
                ActionButton(title: "Merge ticked", icon: "square.on.square") { model.mergeSelected() }
                ActionButton(title: "Frame all", icon: "viewfinder") { model.frameAll() }
            }
        }
    }

    private func selectedSection(_ o: ObjectInfo) -> some View {
        PanelSection("Selected: \(o.name)") {
            HStack {
                TextField("Rename", text: $newName).textFieldStyle(.roundedBorder)
                ActionButton(title: "Rename") { model.rename(o.id, to: newName); newName = "" }
            }
            ButtonGrid(columns: 3) {
                ActionButton(title: "Duplicate") { model.duplicateObject(o.id) }
                ActionButton(title: "Delete", destructive: true) { model.deleteObject(o.id) }
                ActionButton(title: "Solo") { model.solo(o.id) }
                ActionButton(title: "Move up") { model.moveObject(o.id, by: -1) }
                ActionButton(title: "Move down") { model.moveObject(o.id, by: 1) }
                ActionButton(title: "Focus") { model.frameSelection() }
                ActionButton(title: "Separate parts") { model.separateParts() }
                ActionButton(title: "Apply transform") { model.quickOp { sf_op_apply_transform($0, $1) } }
            }
            Menu {
                Button("None (scene root)") { model.setParent(o.id, parent: 0) }
                ForEach(model.objects.filter { $0.id != o.id }) { other in
                    Button(other.name) { model.setParent(o.id, parent: other.id) }
                }
            } label: {
                Label("Parent: " + (model.objects.first { $0.id == o.props.parent }?.name ?? "none"), systemImage: "arrow.turn.down.right").font(.footnote)
            }
            Group {
                SliderRow(title: "Position X", value: model.selectedProp({ $0.position.0 }, { $0.position.0 = $1 }), range: -10...10, onEditing: model.editing)
                SliderRow(title: "Position Y", value: model.selectedProp({ $0.position.1 }, { $0.position.1 = $1 }), range: -10...10, onEditing: model.editing)
                SliderRow(title: "Position Z", value: model.selectedProp({ $0.position.2 }, { $0.position.2 = $1 }), range: -10...10, onEditing: model.editing)
                SliderRow(title: "Scale X", value: model.selectedProp({ $0.scale.0 }, { $0.scale.0 = $1 }), range: 0.05...5, onEditing: model.editing)
                SliderRow(title: "Scale Y", value: model.selectedProp({ $0.scale.1 }, { $0.scale.1 = $1 }), range: 0.05...5, onEditing: model.editing)
                SliderRow(title: "Scale Z", value: model.selectedProp({ $0.scale.2 }, { $0.scale.2 = $1 }), range: 0.05...5, onEditing: model.editing)
            }
            ActionButton(title: "Reset transform", icon: "arrow.counterclockwise") {
                model.editProps(o.id) { $0.position = (0, 0, 0); $0.rotation = (0, 0, 0, 1); $0.scale = (1, 1, 1) }
            }
        }
    }
}

// MARK: - Topology

struct TopologyPanel: View {
    @ObservedObject var model: SceneModel
    @State private var voxel: Float = 0.04
    @State private var keep: Float = 0.5
    @State private var smoothIterations: Float = 3
    @State private var cutAxis = 1
    @State private var cutOffset: Float = 0
    @State private var cutKeepPositive = false
    @State private var cutCap = true
    @State private var booleanOther: UInt32 = 0
    @State private var booleanOp: Int32 = 0
    @State private var confirmRemesh = false

    var body: some View {
        if let o = model.selectedObject {
            PanelSection("Resolution") {
                Text("\(o.name): \(o.vertexCount.formatted()) vertices, \(o.triangleCount.formatted()) triangles").font(.footnote)
                ButtonGrid(columns: 2) {
                    ActionButton(title: "Subdivide (smooth)") { model.subdivide(smooth: true) }
                    ActionButton(title: "Subdivide (linear)") { model.subdivide(smooth: false) }
                }
                SliderRow(title: "Decimate: keep fraction", value: $keep, range: 0.02...0.95)
                ActionButton(title: "Decimate to \(Int(Float(o.triangleCount) * keep).formatted()) triangles") { model.decimate(keepFraction: keep) }
            }
            PanelSection("Voxel remesh") {
                SliderRow(title: "Voxel size", value: $voxel, range: 0.005...0.3, decimals: 3)
                Text("Estimated result: about \(model.estimateRemesh(voxelSize: voxel).formatted()) triangles").font(.caption).foregroundColor(.secondary)
                ActionButton(title: "Remesh", icon: "cube.transparent") { confirmRemesh = true }
                Text("Rebuilds the surface with even triangles and fuses intersecting parts. Colours, roughness, metalness and mask are transferred; face groups are reset.")
                    .font(.caption2).foregroundColor(.secondary)
            }
            .alert("Remesh \(o.name)?", isPresented: $confirmRemesh) {
                Button("Remesh") { model.voxelRemesh(voxelSize: voxel) }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Topology will be replaced. Face groups are reset and details smaller than the voxel size are lost. You can undo this.")
            }
            cleanupSection
            cutSection
            booleanSection
        } else {
            Text("Select an object first.").font(.footnote)
        }
    }

    private var cleanupSection: some View {
        PanelSection("Clean up") {
            SliderRow(title: "Smooth iterations", value: $smoothIterations, range: 1...30, decimals: 0)
            ButtonGrid(columns: 2) {
                ActionButton(title: "Smooth mesh") { model.smoothMesh(iterations: Int(smoothIterations), factor: 0.5) }
                ActionButton(title: "Fill holes") { model.quickOp { sf_op_fill_holes($0, $1) } }
                ActionButton(title: "Flip normals") { model.quickOp { sf_op_flip_normals($0, $1) } }
                ActionButton(title: "Weld vertices") { model.quickOp { sf_op_weld($0, $1, 0.0005) } }
                ActionButton(title: "Mirror X") { model.quickOp { sf_op_mirror($0, $1, 0) } }
                ActionButton(title: "Mirror Y") { model.quickOp { sf_op_mirror($0, $1, 1) } }
                ActionButton(title: "Mirror Z") { model.quickOp { sf_op_mirror($0, $1, 2) } }
            }
            Text("Mirror keeps the positive half of the object (object space), copies it to the other side and welds the seam.").font(.caption2).foregroundColor(.secondary)
        }
    }

    private var cutSection: some View {
        PanelSection("Plane cut") {
            Picker("Axis", selection: $cutAxis) { Text("X").tag(0); Text("Y").tag(1); Text("Z").tag(2) }.pickerStyle(.segmented)
            SliderRow(title: "Offset from centre", value: $cutOffset, range: -3...3)
            Toggle("Keep the positive side", isOn: $cutKeepPositive).font(.footnote)
            Toggle("Cap the cut", isOn: $cutCap).font(.footnote)
            ActionButton(title: "Cut", icon: "scissors") { model.cutPlane(axis: cutAxis, offset: cutOffset, keepPositive: cutKeepPositive, cap: cutCap) }
        }
    }

    private var booleanSection: some View {
        PanelSection("Boolean (voxel based)") {
            Menu {
                ForEach(model.objects.filter { $0.id != model.selection }) { other in
                    Button(other.name) { booleanOther = other.id }
                }
            } label: {
                Label("With: " + (model.objects.first { $0.id == booleanOther }?.name ?? "choose object"), systemImage: "square.on.circle").font(.footnote)
            }
            Picker("Operation", selection: $booleanOp) {
                Text("Union").tag(Int32(0)); Text("Subtract").tag(Int32(1)); Text("Intersect").tag(Int32(2))
            }
            .pickerStyle(.segmented)
            ActionButton(title: "Apply boolean") { model.boolean(with: booleanOther, op: booleanOp, voxelSize: voxel) }
            Text("Uses the voxel size above. The result replaces the selected object; the other object is removed.").font(.caption2).foregroundColor(.secondary)
        }
    }
}

// MARK: - Mask

struct MaskPanel: View {
    @ObservedObject var model: SceneModel
    @State private var thickness: Float = 0.05

    var body: some View {
        PanelSection("Mask brushes") {
            ButtonGrid(columns: 3) {
                ActionButton(title: "Mask") { model.brush.type = 18; model.brush.invert = 0; model.tool = .sculpt }
                ActionButton(title: "Erase") { model.brush.type = 18; model.brush.invert = 1; model.tool = .sculpt }
                ActionButton(title: "Blur") { model.brush.type = 19; model.tool = .sculpt }
                ActionButton(title: "Sharpen") { model.brush.type = 20; model.tool = .sculpt }
                ActionButton(title: "Face group") { model.brush.type = 21; model.tool = .sculpt }
            }
            Toggle("Show mask overlay", isOn: $model.render.showMask).font(.footnote)
        }
        PanelSection("Whole-object mask") {
            ButtonGrid(columns: 3) {
                ActionButton(title: "Clear") { model.maskOp(0) }
                ActionButton(title: "Invert") { model.maskOp(1) }
                ActionButton(title: "Mask all") { model.maskOp(7) }
                ActionButton(title: "Blur") { model.maskOp(2, amount: 2) }
                ActionButton(title: "Sharpen") { model.maskOp(3, amount: 1) }
                ActionButton(title: "Grow") { model.maskOp(4) }
                ActionButton(title: "Shrink") { model.maskOp(5) }
                ActionButton(title: "Cavities") { model.maskOp(6, amount: 1) }
            }
            Text("Masked (dark) areas resist sculpting, smoothing, painting and fills.").font(.caption2).foregroundColor(.secondary)
        }
        PanelSection("Extract") {
            SliderRow(title: "Shell thickness", value: $thickness, range: 0...0.3, decimals: 3)
            ActionButton(title: "Extract masked area as new object", icon: "square.stack.3d.up") { model.extractMask(thickness: thickness) }
            Text("Great for clothing: mask a region of the body, then extract it as a shell.").font(.caption2).foregroundColor(.secondary)
        }
    }
}
