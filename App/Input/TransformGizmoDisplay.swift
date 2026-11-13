// SculptForge 3D — original touch-friendly, world-oriented 3-axis transform gizmo.
// This transparent UIKit view only paints the handles; ViewportMTKView handles touch
// hit-testing so camera gestures remain available everywhere else in the viewport.
import UIKit
import simd

final class TransformGizmoDisplay: UIView {
    private(set) var origin = CGPoint.zero
    private(set) var pivot = SIMD3<Float>(0, 0, 0)
    private(set) var axisEnds: [CGPoint] = [.zero, .zero, .zero]
    private var rings: [[CGPoint]] = [[], [], []]
    private var mode: TransformMode = .move
    private var visible = false
    private let colors: [UIColor] = [
        UIColor(red: 0.96, green: 0.26, blue: 0.24, alpha: 1),
        UIColor(red: 0.29, green: 0.93, blue: 0.37, alpha: 1),
        UIColor(red: 0.32, green: 0.55, blue: 1, alpha: 1)
    ]

    private func projected(_ world: SIMD3<Float>, camera: OrbitCamera, size: CGSize) -> CGPoint? {
        let clip = camera.projectionMatrix * camera.viewMatrix * SIMD4<Float>(world, 1)
        guard clip.w > 0.0001, clip.x.isFinite, clip.y.isFinite else { return nil }
        return CGPoint(x: CGFloat((clip.x / clip.w + 1) * 0.5) * size.width,
                       y: CGFloat((1 - clip.y / clip.w) * 0.5) * size.height)
    }

    func refresh(model: SceneModel, size: CGSize) {
        guard model.tool == .transform, let object = model.selectedObject,
              object.props.visible != 0, size.width > 0, size.height > 0 else {
            if visible { visible = false; setNeedsDisplay() }
            return
        }
        visible = true
        mode = model.transformMode
        // The editable region's weighted centre is used in Masked Transform mode.
        // Otherwise place the gizmo at the scene object's origin.
        pivot = (model.useMaskedTransform ? model.maskPivotWorld : nil)
            ?? SIMD3<Float>(object.world.columns.3.x, object.world.columns.3.y, object.world.columns.3.z)
        let camera = model.camera
        origin = projected(pivot, camera: camera, size: size) ?? .zero
        let depth = simd_dot(pivot - camera.position, camera.forward)
        let worldRadius = camera.worldPerPoint(viewHeight: size.height) * 90
            * (camera.orthographic ? 1 : max(depth / max(camera.distance, 0.001), 0.1))
        let dirs: [SIMD3<Float>] = [SIMD3<Float>(1, 0, 0), SIMD3<Float>(0, 1, 0), SIMD3<Float>(0, 0, 1)]
        for i in 0..<3 {
            axisEnds[i] = projected(pivot + dirs[i] * worldRadius, camera: camera, size: size) ?? origin
        }
        if mode == .rotate {
            let bases: [(SIMD3<Float>, SIMD3<Float>)] = [
                (SIMD3<Float>(0, 1, 0), SIMD3<Float>(0, 0, 1)),
                (SIMD3<Float>(1, 0, 0), SIMD3<Float>(0, 0, 1)),
                (SIMD3<Float>(1, 0, 0), SIMD3<Float>(0, 1, 0))
            ]
            for i in 0..<3 {
                let (a, b) = bases[i]
                rings[i] = (0...64).compactMap { step in
                    let angle = Float(step) * (2 * Float.pi / 64)
                    return projected(pivot + (a * cos(angle) + b * sin(angle)) * worldRadius * 0.88,
                                     camera: camera, size: size)
                }
            }
        }
        setNeedsDisplay()
    }

    private func distance(_ p: CGPoint, to a: CGPoint, _ b: CGPoint) -> CGFloat {
        let dx = b.x - a.x, dy = b.y - a.y
        let len2 = dx * dx + dy * dy
        if len2 < 1 { return hypot(p.x - a.x, p.y - a.y) }
        let t = max(0, min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2))
        return hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy))
    }

    /// -1 = centre/free handle, 0=X, 1=Y, 2=Z; nil = not on the gizmo.
    func handle(at point: CGPoint) -> Int? {
        guard visible else { return nil }
        if mode != .rotate, hypot(point.x - origin.x, point.y - origin.y) < 18 { return -1 }
        var nearest: Int? = nil
        var best: CGFloat = 17
        for i in 0..<3 {
            if mode == .rotate {
                let ring = rings[i]
                guard ring.count >= 2 else { continue }
                for j in 1..<ring.count {
                    let d = distance(point, to: ring[j - 1], ring[j])
                    if d < best { best = d; nearest = i }
                }
            } else {
                let d = distance(point, to: origin, axisEnds[i])
                if d < best { best = d; nearest = i }
            }
        }
        return nearest
    }

    override func draw(_ rect: CGRect) {
        guard visible, let context = UIGraphicsGetCurrentContext() else { return }
        context.setLineCap(.round)
        for i in 0..<3 {
            context.setStrokeColor(colors[i].cgColor)
            context.setFillColor(colors[i].cgColor)
            context.setLineWidth(3.5)
            if mode == .rotate {
                let points = rings[i]
                guard let first = points.first else { continue }
                context.move(to: first)
                for point in points.dropFirst() { context.addLine(to: point) }
                context.strokePath()
            } else {
                context.move(to: origin)
                context.addLine(to: axisEnds[i])
                context.strokePath()
                let end = axisEnds[i]
                let v = CGPoint(x: end.x - origin.x, y: end.y - origin.y)
                let len = max(hypot(v.x, v.y), 1)
                let ux = v.x / len, uy = v.y / len
                if mode == .move {
                    context.move(to: CGPoint(x: end.x + ux * 4, y: end.y + uy * 4))
                    context.addLine(to: CGPoint(x: end.x - ux * 12 - uy * 7, y: end.y - uy * 12 + ux * 7))
                    context.addLine(to: CGPoint(x: end.x - ux * 12 + uy * 7, y: end.y - uy * 12 - ux * 7))
                    context.closePath()
                    context.fillPath()
                } else {
                    context.fill(CGRect(x: end.x - 7, y: end.y - 7, width: 14, height: 14))
                }
            }
        }
        if mode != .rotate {
            context.setFillColor(UIColor.white.cgColor)
            context.fillEllipse(in: CGRect(x: origin.x - 6, y: origin.y - 6, width: 12, height: 12))
            context.setStrokeColor(UIColor.black.cgColor)
            context.setLineWidth(1.5)
            context.strokeEllipse(in: CGRect(x: origin.x - 6, y: origin.y - 6, width: 12, height: 12))
        }
    }
}
