// SculptForge 3D - Metal viewport with touch, Apple Pencil and pointer input.
//
// Gesture rules:
//   * Apple Pencil, or one finger starting on the selected object -> brush stroke / transform drag
//   * one finger starting on empty space                          -> orbit
//   * one finger tap on another object                            -> select it
//   * two fingers                                                 -> pan + pinch zoom (+ twist to roll the orbit)
//   * a second finger landing shortly after a stroke started cancels that stroke (accidental touch)
import MetalKit
import SwiftUI
import UIKit
import simd

final class ViewportMTKView: MTKView {
    weak var model: SceneModel?

    private enum Mode { case idle, stroke, orbit, transform, twoFinger }
    private var mode: Mode = .idle
    private var primary: UITouch?
    private var active: [UITouch] = []
    private var startPoint = CGPoint.zero
    private var startTime: TimeInterval = 0
    private var lastPoint = CGPoint.zero
    private var movedDistance: CGFloat = 0
    private var lastCentroid = CGPoint.zero
    private var lastSpread: CGFloat = 0
    private var lastAngle: CGFloat = 0
    private var transformDepthPoint = SIMD3<Float>(0, 0, 0)
    private var transformStart = SFObjectProps()
    private var activeAxis = -1 // -1 means camera plane / uniform control
    private var gizmoScaleWorld: Float = 1
    private let gizmo = TransformGizmoDisplay()
    private var multiTouchStart: [ObjectIdentifier: CGPoint] = [:]
    private var multiTouchStartTime: TimeInterval = 0
    private var multiTouchMaxCount = 0
    private var multiTouchMoved = false

    func installGestures() {
        isMultipleTouchEnabled = true
        gizmo.isUserInteractionEnabled = false
        gizmo.backgroundColor = .clear
        addSubview(gizmo)
        let hover = UIHoverGestureRecognizer(target: self, action: #selector(hovered(_:)))
        addGestureRecognizer(hover)
        let scroll = UIPanGestureRecognizer(target: self, action: #selector(scrolled(_:)))
        scroll.allowedScrollTypesMask = .all      // trackpad / mouse wheel
        scroll.maximumNumberOfTouches = 0         // never steals finger touches
        addGestureRecognizer(scroll)
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        gizmo.frame = bounds
    }

    func updateGizmoDisplay(_ model: SceneModel) {
        gizmo.refresh(model: model, size: bounds.size)
    }

    private func beginGizmoDrag(_ handle: Int, point: CGPoint, model: SceneModel) {
        guard let props = model.props(of: model.selection), props.locked == 0 else { return }
        mode = .transform
        activeAxis = handle
        transformStart = props
        startPoint = point
        lastPoint = point
        model.beginTransformGesture()
        let depth = simd_dot(gizmo.pivot - model.camera.position, model.camera.forward)
        gizmoScaleWorld = model.camera.worldPerPoint(viewHeight: bounds.height) * 90
            * (model.camera.orthographic ? 1 : max(depth / max(model.camera.distance, 0.001), 0.1))
    }

    private func ray(_ p: CGPoint) -> (origin: SIMD3<Float>, direction: SIMD3<Float>)? {
        guard let model = model else { return nil }
        return model.camera.ray(at: p, viewSize: bounds.size)
    }

    private func pressure(of touch: UITouch) -> Float {
        if touch.type == .pencil, touch.maximumPossibleForce > 0 {
            return Float(max(0.02, min(1, touch.force / touch.maximumPossibleForce * 1.6)))
        }
        return 0.6 // fingers have no reliable force: use a fixed medium pressure
    }

    private func updateCursor(at p: CGPoint) {
        guard let model = model, let r = ray(p) else { return }
        if let hit = model.pick(origin: r.origin, direction: r.direction) {
            model.cursor = BrushCursor(point: hit.point, normal: hit.normal)
        } else {
            model.cursor = nil
        }
    }

    @objc private func hovered(_ g: UIHoverGestureRecognizer) {
        switch g.state {
        case .began, .changed: if mode == .idle { updateCursor(at: g.location(in: self)) }
        default: if mode == .idle { model?.cursor = nil }
        }
    }

    @objc private func scrolled(_ g: UIPanGestureRecognizer) {
        guard let model = model else { return }
        let t = g.translation(in: self)
        g.setTranslation(.zero, in: self)
        model.camera.zoom(factor: Float(exp(Double(t.y) * 0.004)))
    }

    // MARK: - touches

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let model = model, !model.busy else { return }
        for t in touches where !active.contains(t) { active.append(t) }
        if active.count >= 2 {
            if mode != .twoFinger {
                multiTouchStartTime = CACurrentMediaTime()
                multiTouchMoved = false
                multiTouchStart.removeAll()
            }
            for finger in active where multiTouchStart[ObjectIdentifier(finger)] == nil {
                multiTouchStart[ObjectIdentifier(finger)] = finger.location(in: self)
            }
            multiTouchMaxCount = max(active.count, mode == .twoFinger ? multiTouchMaxCount : 0)
            // second finger: stop whatever the first one was doing
            if mode == .stroke {
                let quick = CACurrentMediaTime() - startTime < 0.3
                if quick { model.cancelStroke() } else { model.endStroke() }
            } else if mode == .transform {
                model.endTransformGesture(cancelled: true)
            }
            mode = .twoFinger
            primary = nil
            captureTwoFingerState()
            return
        }
        guard mode == .idle, let touch = touches.first else { return }
        primary = touch
        let p = touch.location(in: self)
        // Axis hit-testing is done in the native viewport, so the gizmo remains
        // visible but does not steal pinch, pan or Pencil touches.
        if model.tool == .transform, model.selection != 0,
           let handle = gizmo.handle(at: p) {
            primary = touch
            beginGizmoDrag(handle, point: p, model: model)
            return
        }
        startPoint = p
        lastPoint = p
        movedDistance = 0
        startTime = CACurrentMediaTime()
        guard let r = ray(p) else { return }
        let hit = model.pick(origin: r.origin, direction: r.direction)
        let isPencil = touch.type == .pencil
        let mayEdit = isPencil || model.settings.fingerSculpts
        if mayEdit, let hit = hit, hit.id == model.selection {
            if model.tool == .sculpt {
                if model.beginStroke(origin: r.origin, direction: r.direction, pressure: pressure(of: touch)) {
                    mode = .stroke
                    model.cursor = BrushCursor(point: hit.point, normal: hit.normal)
                    return
                }
            } else if let props = model.props(of: model.selection), props.locked == 0 {
                mode = .transform
                transformDepthPoint = hit.point
                transformStart = props
                activeAxis = -1
                model.beginTransformGesture()
                return
            }
        }
        mode = .orbit
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let model = model else { return }
        if mode == .twoFinger {
            for finger in active {
                if let start = multiTouchStart[ObjectIdentifier(finger)],
                   hypot(finger.location(in: self).x - start.x,
                         finger.location(in: self).y - start.y) > 12 { multiTouchMoved = true }
            }
            if multiTouchMoved { updateTwoFinger() }
            return
        }
        guard let touch = primary, touches.contains(touch) else { return }
        let p = touch.location(in: self)
        let dx = p.x - lastPoint.x, dy = p.y - lastPoint.y
        movedDistance += abs(dx) + abs(dy)
        switch mode {
        case .stroke:
            // coalesced touches carry the full-rate Pencil samples (up to 240 Hz)
            let samples = event?.coalescedTouches(for: touch) ?? [touch]
            for s in samples {
                guard let r = ray(s.location(in: self)) else { continue }
                model.moveStroke(origin: r.origin, direction: r.direction, pressure: pressure(of: s))
            }
            updateCursor(at: p)
        case .orbit:
            model.camera.orbit(dx: Float(dx), dy: Float(dy))
        case .transform:
            applyTransformDrag(to: p, dx: dx, dy: dy)
        default:
            break
        }
        lastPoint = p
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { finish(touches, cancelled: false) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { finish(touches, cancelled: true) }

    private func finish(_ touches: Set<UITouch>, cancelled: Bool) {
        guard let model = model else { active.removeAll(); mode = .idle; return }
        active.removeAll { touches.contains($0) }
        if let touch = primary, touches.contains(touch) {
            switch mode {
            case .stroke:
                if cancelled { model.cancelStroke() } else { model.endStroke() }
            case .transform:
                model.endTransformGesture(cancelled: cancelled)
            case .orbit:
                // a short tap without movement selects the object under the finger
                if !cancelled, movedDistance < 8, CACurrentMediaTime() - startTime < 0.35, let r = ray(startPoint),
                   let hit = model.pick(origin: r.origin, direction: r.direction), hit.id != model.selection {
                    model.selection = hit.id
                }
            default:
                break
            }
            primary = nil
            mode = .idle
            if touch.type != .pencil { model.cursor = nil }
        }
        if mode == .twoFinger {
            if active.count >= 2 { captureTwoFingerState() }
            else if active.isEmpty {
                if !cancelled && !multiTouchMoved && CACurrentMediaTime() - multiTouchStartTime < 0.36 {
                    if multiTouchMaxCount == 2 { model.undo() }
                    else if multiTouchMaxCount == 3 { model.redo() }
                }
                mode = .idle
                multiTouchStart.removeAll()
                multiTouchMaxCount = 0
            }
            // one finger left: stay in navigation until it lifts, so it cannot start a stroke
        }
    }

    // MARK: - two-finger navigation

    private func twoFingerState() -> (centroid: CGPoint, spread: CGFloat, angle: CGFloat)? {
        guard active.count >= 2 else { return nil }
        let a = active[0].location(in: self), b = active[1].location(in: self)
        return (CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2), hypot(b.x - a.x, b.y - a.y), atan2(b.y - a.y, b.x - a.x))
    }

    private func captureTwoFingerState() {
        guard let s = twoFingerState() else { return }
        lastCentroid = s.centroid
        lastSpread = max(s.spread, 1)
        lastAngle = s.angle
    }

    private func updateTwoFinger() {
        guard let model = model, let s = twoFingerState() else { return }
        model.camera.pan(dx: Float(s.centroid.x - lastCentroid.x), dy: Float(s.centroid.y - lastCentroid.y), viewHeight: bounds.height)
        model.camera.zoom(factor: Float(max(s.spread, 1) / lastSpread))
        var da = s.angle - lastAngle
        if da > .pi { da -= 2 * .pi } else if da < -.pi { da += 2 * .pi }
        model.camera.yaw += Float(da)
        lastCentroid = s.centroid
        lastSpread = max(s.spread, 1)
        lastAngle = s.angle
    }

    // MARK: - transform tool

    private func applyTransformDrag(to p: CGPoint, dx: CGFloat, dy: CGFloat) {
        guard let model = model, model.selection != 0 else { return }
        let cam = model.camera
        let drag = SIMD2<Float>(Float(p.x - startPoint.x), Float(p.y - startPoint.y))
        let axis: SIMD3<Float> = activeAxis >= 0
            ? [SIMD3<Float>(1, 0, 0), SIMD3<Float>(0, 1, 0), SIMD3<Float>(0, 0, 1)][activeAxis]
            : cam.up
        var amount = Float(drag.x - drag.y) * 0.7
        if activeAxis >= 0 {
            let a = gizmo.axisEnds[activeAxis]
            let screenAxis = SIMD2<Float>(Float(a.x - gizmo.origin.x), Float(a.y - gizmo.origin.y))
            let len = simd_length(screenAxis)
            if len > 5 { amount = simd_dot(drag, screenAxis / len) }
        }
        let worldPerPixel = gizmoScaleWorld / 90
        let translation: SIMD3<Float>
        if activeAxis < 0 {
            translation = cam.right * (drag.x * worldPerPixel) - cam.up * (drag.y * worldPerPixel)
        } else { translation = axis * (amount * worldPerPixel) }
        switch model.transformMode {
        case .move:
            if model.maskedGestureActive { model.updateMaskedTransform(translation: translation) }
            else {
                let s = transformStart.position
                model.setProps(model.selection) { $0.position = (s.0 + translation.x, s.1 + translation.y, s.2 + translation.z) }
            }
        case .rotate:
            let angle = amount * 0.013
            if model.maskedGestureActive { model.updateMaskedTransform(axis: axis, angle: angle) }
            else {
                let q = simd_quatf(angle: angle, axis: axis)
                let startQ = simd_quatf(ix: transformStart.rotation.0, iy: transformStart.rotation.1,
                                       iz: transformStart.rotation.2, r: transformStart.rotation.3)
                let result = simd_normalize(q * startQ)
                model.setProps(model.selection) { $0.rotation = (result.imag.x, result.imag.y, result.imag.z, result.real) }
            }
        case .scale:
            let f = max(0.1, min(10, Float(exp(Double(amount) * 0.009))))
            var factors = SIMD3<Float>(repeating: 1)
            if activeAxis < 0 { factors = SIMD3<Float>(repeating: f) }
            else { factors[activeAxis] = f }
            if model.maskedGestureActive { model.updateMaskedTransform(scaling: factors) }
            else {
                let s = transformStart.scale
                model.setProps(model.selection) {
                    $0.scale = (s.0 * factors.x, s.1 * factors.y, s.2 * factors.z)
                }
            }
        }
    }

}

struct ViewportView: UIViewRepresentable {
    let model: SceneModel

    final class Coordinator {
        var renderer: Renderer?
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeUIView(context: Context) -> ViewportMTKView {
        let view = ViewportMTKView(frame: .zero)
        view.model = model
        view.installGestures()
        if let renderer = Renderer(view: view, model: model) {
            context.coordinator.renderer = renderer
            view.delegate = renderer
            model.renderer = renderer
        } else {
            let m = model
            DispatchQueue.main.async { if m.message == nil { m.message = "Metal is not available on this device." } }
        }
        return view
    }

    func updateUIView(_ uiView: ViewportMTKView, context: Context) {}
}
