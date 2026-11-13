// SculptForge 3D - small reusable controls and binding helpers
import SwiftUI
import UIKit

struct SliderRow: View {
    let title: String
    @Binding var value: Float
    let range: ClosedRange<Float>
    var decimals: Int = 2
    var onEditing: ((Bool) -> Void)? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).font(.caption)
                Spacer()
                Text(String(format: "%.\(decimals)f", Double(value))).font(.caption.monospacedDigit()).foregroundColor(.secondary)
            }
            Slider(value: $value, in: range, onEditingChanged: { editing in onEditing?(editing) })
                .accessibilityLabel(Text(title))
        }
    }
}

struct PanelSection<Content: View>: View {
    let title: String
    let content: Content

    init(_ title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title.uppercased()).font(.caption2.weight(.bold)).foregroundColor(.secondary)
            content
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color.white.opacity(0.06))
        .cornerRadius(10)
    }
}

/// Compact bordered action button used throughout the panels.
struct ActionButton: View {
    let title: String
    var icon: String? = nil
    var destructive = false
    let action: () -> Void

    var body: some View {
        Button(role: destructive ? .destructive : nil, action: action) {
            HStack(spacing: 4) {
                if let icon = icon { Image(systemName: icon) }
                Text(title).lineLimit(1).minimumScaleFactor(0.7)
            }
            .font(.footnote)
        }
        .buttonStyle(.bordered)
    }
}

/// Lays children out in rows that wrap (simple fixed-column grid keeps the type checker fast).
struct ButtonGrid<Content: View>: View {
    var columns = 2
    let content: Content
    init(columns: Int = 2, @ViewBuilder content: () -> Content) {
        self.columns = columns
        self.content = content()
    }
    var body: some View {
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: columns), alignment: .leading, spacing: 6) {
            content
        }
    }
}

struct ShareItem: Identifiable {
    let id = UUID()
    let url: URL
}

/// System share sheet: includes "Save to Files", AirDrop and other apps.
struct ActivityView: UIViewControllerRepresentable {
    let items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

extension SceneModel {
    func brushFlag(_ keyPath: WritableKeyPath<SFBrush, Int32>) -> Binding<Bool> {
        Binding(get: { self.brush[keyPath: keyPath] != 0 }, set: { self.brush[keyPath: keyPath] = $0 ? 1 : 0 })
    }
    func brushValue(_ keyPath: WritableKeyPath<SFBrush, Float>) -> Binding<Float> {
        Binding(get: { self.brush[keyPath: keyPath] }, set: { self.brush[keyPath: keyPath] = $0 })
    }
    var brushColorBinding: Binding<Color> {
        Binding(get: { Color(linear: SIMD3<Float>(self.brush.color.0, self.brush.color.1, self.brush.color.2)) },
                set: { let c = $0.linearRGB; self.brush.color = (c.x, c.y, c.z) })
    }
    var brushName: String { String(cString: sf_brush_name(brush.type)) }

    /// Binding to one float property of the selected object (no undo step: wrap in begin/end edit).
    func selectedProp(_ get: @escaping (SFObjectProps) -> Float, _ set: @escaping (inout SFObjectProps, Float) -> Void) -> Binding<Float> {
        Binding(get: { self.selectedObject.map { get($0.props) } ?? 0 },
                set: { v in self.setProps(self.selection) { set(&$0, v) } })
    }
    func editing(_ active: Bool) {
        if active { beginPropsEdit(selection) } else { endPropsEdit(selection) }
    }
}

enum BrushIcons {
    static let names = ["pencil.tip", "square.stack.3d.up", "square.stack.3d.up.fill", "plus.circle", "drop", "rectangle.compress.vertical",
                        "sparkles", "minus.rectangle", "chevron.down", "arrow.down.right.and.arrow.up.left", "hand.draw",
                        "hand.point.up.left", "scribble", "arrow.right", "arrow.up.and.down.and.arrow.left.and.right",
                        "arrow.triangle.2.circlepath", "square.3.layers.3d", "seal", "theatermasks", "aqi.medium",
                        "circle.lefthalf.filled", "square.grid.2x2", "paintbrush", "hand.point.up", "paintpalette"]
    static func icon(_ type: Int32) -> String {
        let i = Int(type)
        return i >= 0 && i < names.count ? names[i] : "circle"
    }
    static func title(_ type: Int32) -> String { String(cString: sf_brush_name(type)) }
    static let hints = [
        "Push the surface outward along its normal", "Fill up to a plane above the surface", "Build up chunky clay strips",
        "Swell the surface along each vertex normal", "Relax bumps and uneven geometry", "Press the surface onto its average plane",
        "Flatten and smooth for clean planes", "Shave off peaks above the surface plane", "Cut a sharp groove and pinch its edges",
        "Pull vertices toward the stroke centre", "Grab a region and drag it", "Grab with a wide, soft elastic falloff",
        "Pull geometry out into horns and tendrils", "Slide the surface along itself", "Drag the surface with the stroke",
        "Twist the surface around the brush centre", "Raise the surface to a fixed layer height", "Stamp one dab where you touch",
        "Paint a mask that protects the surface", "Soften mask edges", "Harden mask edges", "Assign faces to a face group",
        "Paint vertex colour", "Smear existing colours", "Paint colour, roughness and metalness"]
    static func hint(_ type: Int32) -> String {
        let i = Int(type)
        return i >= 0 && i < hints.count ? hints[i] : ""
    }
}
