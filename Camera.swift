// SculptForge 3D - orbit camera (perspective and orthographic)
import CoreGraphics
import Foundation
import simd

enum ViewPreset: String, CaseIterable {
    case front = "Front", back = "Back", left = "Left", right = "Right", top = "Top", bottom = "Bottom"
}

struct OrbitCamera {
    var target = SIMD3<Float>(0, 0, 0)
    var distance: Float = 5
    var yaw: Float = 0.5       // radians around world Y
    var pitch: Float = 0.3     // radians, positive looks down from above
    var fovDegrees: Float = 40
    var orthographic = false
    var aspect: Float = 1

    private var rotation: simd_quatf {
        simd_quatf(angle: yaw, axis: SIMD3<Float>(0, 1, 0)) * simd_quatf(angle: -pitch, axis: SIMD3<Float>(1, 0, 0))
    }
    var forward: SIMD3<Float> { rotation.act(SIMD3<Float>(0, 0, -1)) }
    var right: SIMD3<Float> { rotation.act(SIMD3<Float>(1, 0, 0)) }
    var up: SIMD3<Float> { rotation.act(SIMD3<Float>(0, 1, 0)) }
    var position: SIMD3<Float> { target - forward * distance }
    var nearPlane: Float { max(distance * 0.02, 0.001) }
    var farPlane: Float { distance * 50 + 200 }
    private var tanHalfFov: Float { tan(fovDegrees * .pi / 360) }
    /// Half extents of the view volume at the target distance.
    var halfHeight: Float { distance * tanHalfFov }
    var halfWidth: Float { halfHeight * aspect }

    var viewMatrix: float4x4 {
        let f = forward, s = right, u = up, eye = position
        return float4x4(columns: (SIMD4<Float>(s.x, u.x, -f.x, 0),
                                  SIMD4<Float>(s.y, u.y, -f.y, 0),
                                  SIMD4<Float>(s.z, u.z, -f.z, 0),
                                  SIMD4<Float>(-dot(s, eye), -dot(u, eye), dot(f, eye), 1)))
    }

    /// Right-handed projection with Metal's 0...1 depth range.
    var projectionMatrix: float4x4 {
        let n = nearPlane, f = farPlane
        if orthographic {
            return float4x4(columns: (SIMD4<Float>(1 / halfWidth, 0, 0, 0),
                                      SIMD4<Float>(0, 1 / halfHeight, 0, 0),
                                      SIMD4<Float>(0, 0, 1 / (n - f), 0),
                                      SIMD4<Float>(0, 0, n / (n - f), 1)))
        }
        let ys = 1 / tanHalfFov, xs = ys / aspect, zs = f / (n - f)
        return float4x4(columns: (SIMD4<Float>(xs, 0, 0, 0),
                                  SIMD4<Float>(0, ys, 0, 0),
                                  SIMD4<Float>(0, 0, zs, -1),
                                  SIMD4<Float>(0, 0, zs * n, 0)))
    }

    /// World-space ray through a point of the view (points, origin top-left).
    func ray(at point: CGPoint, viewSize: CGSize) -> (origin: SIMD3<Float>, direction: SIMD3<Float>) {
        let w = max(Float(viewSize.width), 1), h = max(Float(viewSize.height), 1)
        let nx = 2 * Float(point.x) / w - 1
        let ny = 1 - 2 * Float(point.y) / h
        if orthographic {
            return (position + right * (nx * halfWidth) + up * (ny * halfHeight), forward)
        }
        let dir = simd_normalize(forward + right * (nx * tanHalfFov * aspect) + up * (ny * tanHalfFov))
        return (position, dir)
    }

    /// World units covered by one point of screen movement at the target distance.
    func worldPerPoint(viewHeight: CGFloat) -> Float { 2 * halfHeight / max(Float(viewHeight), 1) }

    mutating func orbit(dx: Float, dy: Float) {
        yaw -= dx * 0.008
        pitch = max(-1.55, min(1.55, pitch + dy * 0.008))
    }

    mutating func pan(dx: Float, dy: Float, viewHeight: CGFloat) {
        let k = worldPerPoint(viewHeight: viewHeight)
        target += (-right * dx + up * dy) * k
    }

    mutating func zoom(factor: Float) {
        guard factor.isFinite, factor > 0 else { return }
        distance = max(0.01, min(10000, distance / factor))
    }

    mutating func frame(min mn: SIMD3<Float>, max mx: SIMD3<Float>) {
        guard mn.x <= mx.x else { return }
        target = (mn + mx) * 0.5
        let radius = max(simd_length(mx - mn) * 0.5, 0.01)
        distance = radius / max(sin(fovDegrees * .pi / 360), 0.05) * 1.1
    }

    mutating func setView(_ preset: ViewPreset) {
        switch preset {
        case .front: yaw = 0; pitch = 0
        case .back: yaw = .pi; pitch = 0
        case .right: yaw = .pi / 2; pitch = 0
        case .left: yaw = -.pi / 2; pitch = 0
        case .top: yaw = 0; pitch = 1.55
        case .bottom: yaw = 0; pitch = -1.55
        }
    }
}
