// SculptForge 3D - main screen: full-bleed viewport with floating toolbars and one side panel
import Combine
import SwiftUI

enum Panel: String, CaseIterable, Identifiable {
    case brush = "Brush", scene = "Objects", topology = "Topology", mask = "Mask", paint = "Paint", material = "Material"
    case render = "Render", files = "Files", references = "References", print = "3D Print", settings = "Settings"
    var id: String { rawValue }
    var icon: String {
        switch self {
        case .brush: return "paintbrush.pointed"
        case .scene: return "cube"
        case .topology: return "square.grid.3x3"
        case .mask: return "theatermasks"
        case .paint: return "paintpalette"
        case .material: return "circle.lefthalf.filled"
        case .render: return "sun.max"
        case .files: return "folder"
        case .references: return "photo.on.rectangle.angled"
        case .print: return "printer"
        case .settings: return "gearshape"
        }
    }
}

struct RootView: View {
    @StateObject private var model = SceneModel()
    @State private var panel: Panel? = nil
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        GeometryReader { geo in
            let compact = geo.size.width < 700
            ZStack {
                ViewportView(model: model).ignoresSafeArea()
                ReferenceOverlayCanvas(model: model)
                VStack(spacing: 0) {
                    TopBar(model: model, panel: $panel, compact: compact)
                    HStack(alignment: .top, spacing: 0) {
                        if !model.settings.leftHanded { brushBar }
                        Spacer(minLength: 0)
                        if !compact, let p = panel { sidePanel(p).frame(width: 330) }
                        if model.settings.leftHanded { brushBar }
                    }
                    .frame(maxHeight: .infinity)
                    if compact, let p = panel, p != .mask { sidePanel(p).frame(height: geo.size.height * 0.42) }
                    if panel == .mask || (model.tool == .sculpt && [18, 19, 20].contains(model.brush.type)) {
                        MaskQuickBar(model: model)
                    }
                    BottomBar(model: model)
                }
                if model.busy { BusyOverlay(model: model) }
            }
        }
        .preferredColorScheme(.dark)
        .alert("SculptForge", isPresented: Binding(get: { model.message != nil }, set: { if !$0 { model.message = nil } })) {
            Button("OK", role: .cancel) { model.message = nil }
        } message: {
            Text(model.message ?? "")
        }
        .alert("Recover unsaved work?", isPresented: $model.recoveryAvailable) {
            Button("Recover") { model.recoverAutosave() }
            Button("Discard", role: .destructive) { model.discardAutosave() }
        } message: {
            Text("SculptForge found an autosaved project from a session that did not close normally.")
        }
        .onOpenURL { url in model.importFile(url) } // .sculptforge / mesh files opened from the Files app
        .onChange(of: scenePhase) { phase in
            // going to the background: finish the stroke and write a recovery copy right away
            if phase != .active { model.endStroke(); model.autosaveIfNeeded() }
        }
    }

    private var brushBar: some View {
        Group {
            if model.tool == .sculpt { BrushBar(model: model) } else { TransformBar(model: model) }
        }
    }

    private func sidePanel(_ p: Panel) -> some View {
        VStack(spacing: 0) {
            HStack {
                Label(p.rawValue, systemImage: p.icon).font(.headline)
                Spacer()
                Button { panel = nil } label: { Image(systemName: "xmark.circle.fill").font(.title3) }
                    .accessibilityLabel("Close panel")
            }
            .padding(.horizontal, 12).padding(.vertical, 8)
            ScrollView {
                VStack(spacing: 10) {
                    switch p {
                    case .brush: BrushPanel(model: model)
                    case .scene: ScenePanel(model: model)
                    case .topology: TopologyPanel(model: model)
                    case .mask: MaskPanel(model: model)
                    case .paint: PaintPanel(model: model)
                    case .material: MaterialPanel(model: model)
                    case .render: RenderPanel(model: model)
                    case .files: FilesPanel(model: model)
                    case .references: ReferencesPanel(model: model)
                    case .print: PrintPanel(model: model)
                    case .settings: SettingsPanel(model: model)
                    }
                }
                .padding(10)
            }
        }
        .background(.ultraThinMaterial)
        .cornerRadius(14)
        .padding(6)
        .disabled(model.busy)
    }
}

struct TopBar: View {
    @ObservedObject var model: SceneModel
    @Binding var panel: Panel?
    let compact: Bool

    var body: some View {
        HStack(spacing: 8) {
            Button { model.undo() } label: { Image(systemName: "arrow.uturn.backward") }
                .disabled(!model.canUndo || model.busy).keyboardShortcut("z", modifiers: .command).accessibilityLabel("Undo")
            Button { model.redo() } label: { Image(systemName: "arrow.uturn.forward") }
                .disabled(!model.canRedo || model.busy).keyboardShortcut("z", modifiers: [.command, .shift]).accessibilityLabel("Redo")
            Picker("Tool", selection: $model.tool) {
                ForEach(Tool.allCases, id: \.self) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented).frame(width: compact ? 150 : 190)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: compact ? 10 : 14) {
                    ForEach(Panel.allCases) { p in
                        Button { panel = panel == p ? nil : p } label: {
                            VStack(spacing: 1) {
                                Image(systemName: p.icon)
                                if !compact { Text(p.rawValue).font(.system(size: 9)) }
                            }
                            .foregroundColor(panel == p ? .orange : .primary)
                        }
                        .accessibilityLabel(p.rawValue)
                    }
                }
                .padding(.horizontal, 4)
            }
            if !compact {
                VStack(alignment: .trailing, spacing: 0) {
                    Text(model.projectName + (model.isDirty ? " •" : "")).font(.caption.weight(.semibold)).lineLimit(1)
                    Text("\(model.totalTriangles.formatted()) tris · \(Int(model.memoryMB)) MB").font(.caption2.monospacedDigit()).foregroundColor(.secondary)
                }
            }
            Button { model.save() } label: { Image(systemName: "square.and.arrow.down") }
                .keyboardShortcut("s", modifiers: .command).disabled(model.busy).accessibilityLabel("Save project")
        }
        .font(.body)
        .padding(.horizontal, 10).padding(.vertical, 6)
        .background(.ultraThinMaterial)
    }
}

struct BrushBar: View {
    @ObservedObject var model: SceneModel

    var body: some View {
        ScrollView(.vertical, showsIndicators: false) {
            VStack(spacing: 4) {
                ForEach(0..<Int(sf_brush_count()), id: \.self) { i in
                    let type = Int32(i)
                    Button { model.brush.type = type } label: {
                        VStack(spacing: 1) {
                            Image(systemName: BrushIcons.icon(type)).font(.system(size: 17))
                            Text(BrushIcons.title(type)).font(.system(size: 8)).lineLimit(1).minimumScaleFactor(0.6)
                        }
                        .frame(width: 52, height: 40)
                        .background(model.brush.type == type ? Color.orange.opacity(0.85) : Color.white.opacity(0.07))
                        .foregroundColor(model.brush.type == type ? .black : .primary)
                        .cornerRadius(8)
                    }
                    .accessibilityLabel(BrushIcons.title(type))
                    .accessibilityHint(BrushIcons.hint(type))
                }
            }
            .padding(5)
        }
        .frame(width: 64)
        .background(.ultraThinMaterial)
        .cornerRadius(12)
        .padding(6)
    }
}

struct TransformBar: View {
    @ObservedObject var model: SceneModel
    private func icon(_ m: TransformMode) -> String {
        switch m {
        case .move: return "arrow.up.and.down.and.arrow.left.and.right"
        case .rotate: return "arrow.triangle.2.circlepath"
        case .scale: return "arrow.up.left.and.arrow.down.right"
        }
    }
    var body: some View {
        VStack(spacing: 4) {
            ForEach(TransformMode.allCases, id: \.self) { m in
                Button { model.transformMode = m } label: {
                    VStack(spacing: 1) {
                        Image(systemName: icon(m)).font(.system(size: 17))
                        Text(m.rawValue).font(.system(size: 9))
                    }
                    .frame(width: 52, height: 42)
                    .background(model.transformMode == m ? Color.orange.opacity(0.85) : Color.white.opacity(0.07))
                    .foregroundColor(model.transformMode == m ? .black : .primary)
                    .cornerRadius(8)
                }
                .accessibilityLabel(m.rawValue)
            }
            Divider().frame(width: 40)
            Toggle("Mask", isOn: $model.useMaskedTransform)
                .toggleStyle(.button).font(.caption2)
                .accessibilityLabel("Deform unmasked geometry")
            Button { model.editProps(model.selection) { $0.position = (0, 0, 0); $0.rotation = (0, 0, 0, 1); $0.scale = (1, 1, 1) } } label: {
                VStack(spacing: 1) { Image(systemName: "arrow.counterclockwise"); Text("Reset").font(.system(size: 9)) }.frame(width: 52, height: 42)
            }
            Button { model.frameSelection() } label: {
                VStack(spacing: 1) { Image(systemName: "scope"); Text("Focus").font(.system(size: 9)) }.frame(width: 52, height: 42)
            }
        }
        .padding(5)
        .background(.ultraThinMaterial)
        .cornerRadius(12)
        .padding(6)
    }
}

struct BottomBar: View {
    @ObservedObject var model: SceneModel

    var body: some View {
        HStack(spacing: 12) {
            if model.tool == .sculpt {
                VStack(alignment: .leading, spacing: 0) {
                    Text(model.brushName).font(.caption.weight(.semibold))
                    Text(model.selectedObject?.name ?? "No object").font(.caption2).foregroundColor(.secondary).lineLimit(1)
                }
                .frame(width: 86, alignment: .leading)
                SliderRow(title: "Radius", value: model.brushValue(\.radius), range: 0.01...1.5)
                SliderRow(title: "Intensity", value: model.brushValue(\.strength), range: 0...1)
                Toggle("Invert", isOn: model.brushFlag(\.invert)).toggleStyle(.button).font(.caption)
                Toggle("Sym X", isOn: model.brushFlag(\.symX)).toggleStyle(.button).font(.caption)
            } else {
                HStack(spacing: 8) {
                    Toggle("Masked transform", isOn: $model.useMaskedTransform).font(.caption2)
                    Text(model.useMaskedTransform && model.maskPivotWorld != nil
                         ? "Grey = locked • move the unmasked area with the coloured gizmo"
                         : "Object transform • drag the X/Y/Z handles")
                        .font(.caption2).foregroundColor(.secondary)
                        .lineLimit(2)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .padding(.horizontal, 12).padding(.vertical, 6)
        .background(.ultraThinMaterial)
    }
}

struct BusyOverlay: View {
    @ObservedObject var model: SceneModel
    @State private var progress: Float = 0
    private let timer = Timer.publish(every: 0.15, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            Color.black.opacity(0.45).ignoresSafeArea()
            VStack(spacing: 12) {
                Text(model.busyTitle).font(.headline)
                // real progress reported by the engine (0 until an operation reports any)
                if progress > 0 { ProgressView(value: Double(min(progress, 1))).frame(width: 220) } else { ProgressView() }
                Button("Cancel") { gCancelRequested = true }.buttonStyle(.bordered)
            }
            .padding(24)
            .background(.regularMaterial)
            .cornerRadius(16)
        }
        .onReceive(timer) { _ in progress = gOperationProgress }
    }
}


// Immediate bottom mask controls — no nested menus required to reach Invert.
struct MaskQuickBar: View {
    @ObservedObject var model: SceneModel
    private func choose(_ brush: Int32, erase: Bool = false) {
        model.brush.type = brush
        model.brush.invert = erase ? 1 : 0
        model.tool = .sculpt
        model.render.showMask = true
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 8) {
                Text("MASK").font(.caption2.weight(.bold)).foregroundColor(.orange)
                Text("Grey = protected; normal colour = editable")
                    .font(.caption2).foregroundColor(.secondary)
                Spacer(minLength: 0)
                Toggle("Show", isOn: $model.render.showMask).font(.caption2)
            }
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 7) {
                    Button("Paint") { choose(18) }
                    Button("Erase") { choose(18, erase: true) }
                    Button("Invert") { model.maskOp(1) }
                        .tint(.orange).buttonStyle(.borderedProminent)
                    Button("Clear") { model.maskOp(0) }
                    Button("Blur") { model.maskOp(2, amount: 2) }
                    Button("Sharpen") { model.maskOp(3) }
                    Button("Grow") { model.maskOp(4) }
                    Button("Shrink") { model.maskOp(5) }
                    Button("All") { model.maskOp(7) }
                }
                .buttonStyle(.bordered)
                .font(.caption)
            }
            HStack {
                Text("Overlay").font(.caption2)
                Slider(value: $model.maskOverlayOpacity, in: 0.15...1)
                    .accessibilityLabel("Mask overlay opacity")
            }
        }
        .padding(.horizontal, 10).padding(.vertical, 6)
        .background(.ultraThinMaterial)
        .disabled(model.busy)
    }
}
