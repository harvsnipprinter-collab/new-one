// SculptForge 3D - Metal renderer. Draws the engine's real mesh buffers; nothing here is pre-baked.
import Foundation
import MetalKit
import simd

struct FrameUniforms {
    var viewProj = matrix_identity_float4x4
    var view = matrix_identity_float4x4
    var cameraPos = SIMD4<Float>(0, 0, 0, 0)
    var lightDir0 = SIMD4<Float>(0, 1, 0, 0), lightDir1 = SIMD4<Float>(0, 1, 0, 0)
    var lightDir2 = SIMD4<Float>(0, 1, 0, 0), lightDir3 = SIMD4<Float>(0, 1, 0, 0)
    var lightColor0 = SIMD4<Float>(0, 0, 0, 0), lightColor1 = SIMD4<Float>(0, 0, 0, 0)
    var lightColor2 = SIMD4<Float>(0, 0, 0, 0), lightColor3 = SIMD4<Float>(0, 0, 0, 0)
    var ambientSky = SIMD4<Float>(0.5, 0.5, 0.5, 1)
    var ambientGround = SIMD4<Float>(0.2, 0.2, 0.2, 0)
    var params = SIMD4<Float>(1, 0, 1, 0)
    var backgroundTop = SIMD4<Float>(0.2, 0.2, 0.2, 1)
    var backgroundBottom = SIMD4<Float>(0.05, 0.05, 0.05, 1)
}

struct ObjectUniforms {
    var model = matrix_identity_float4x4
    var normalMatrix = matrix_identity_float4x4
    var baseColor = SIMD4<Float>(1, 1, 1, 1)
    var material = SIMD4<Float>(0.6, 0, 0, 0)
    var flags = SIMD4<Float>(0, 0, 0, 0)
}

struct LineVertex {
    var position: SIMD3<Float>   // 16 bytes (SIMD3 is padded)
    var color: SIMD4<Float>
}

/// GPU copies of one object's vertex streams (one Metal buffer per engine array) and its indices.
final class GPUMesh {
    var streams: [MTLBuffer] = []     // positions, normals, colours, roughness, metalness, mask
    var indexBuffer: MTLBuffer?
    var vertexCount = 0
    var indexCount = 0
    var topologyVersion: UInt64 = 0
    static let strides = [12, 12, 12, 4, 4, 4]

    private func pointers(_ v: SFMeshView) -> [UnsafeRawPointer?] {
        [UnsafeRawPointer(v.positions), UnsafeRawPointer(v.normals), UnsafeRawPointer(v.colors),
         UnsafeRawPointer(v.roughness), UnsafeRawPointer(v.metalness), UnsafeRawPointer(v.mask)]
    }

    /// Recreate buffers if the topology changed, then copy everything.
    func uploadAll(_ v: SFMeshView, device: MTLDevice) {
        let vc = Int(v.vertexCount), ic = Int(v.triangleCount) * 3
        guard vc > 0, ic > 0, let indexSource = v.indices else { vertexCount = 0; indexCount = 0; return }
        if streams.count != 6 || vc > (streams.first?.length ?? 0) / 12 || vc < vertexCount / 2 {
            let capacity = vc + vc / 8 + 64
            streams = GPUMesh.strides.compactMap { device.makeBuffer(length: capacity * $0, options: .storageModeShared) }
        }
        if indexBuffer == nil || ic * 4 > indexBuffer!.length || ic < indexCount / 2 {
            indexBuffer = device.makeBuffer(length: (ic + ic / 8 + 96) * 4, options: .storageModeShared)
        }
        guard streams.count == 6, let ib = indexBuffer else { vertexCount = 0; indexCount = 0; return }
        let src = pointers(v)
        for i in 0..<6 {
            if let p = src[i] { streams[i].contents().copyMemory(from: p, byteCount: vc * GPUMesh.strides[i]) }
        }
        ib.contents().copyMemory(from: UnsafeRawPointer(indexSource), byteCount: ic * 4)
        vertexCount = vc
        indexCount = ic
        topologyVersion = v.topologyVersion
    }

    /// Copy only a vertex range (used while a stroke is in progress).
    func uploadRange(_ v: SFMeshView, first: Int, last: Int) {
        guard streams.count == 6, Int(v.vertexCount) == vertexCount, first <= last, last < vertexCount else { return }
        let src = pointers(v)
        for i in 0..<6 {
            let s = GPUMesh.strides[i]
            if let p = src[i] {
                streams[i].contents().advanced(by: first * s).copyMemory(from: p.advanced(by: first * s), byteCount: (last - first + 1) * s)
            }
        }
    }
}

final class Renderer: NSObject, MTKViewDelegate {
    let device: MTLDevice
    private let queue: MTLCommandQueue
    private var meshPipeline: MTLRenderPipelineState!
    private var backgroundPipeline: MTLRenderPipelineState!
    private var linePipeline: MTLRenderPipelineState!
    private var depthWrite: MTLDepthStencilState!
    private var depthRead: MTLDepthStencilState!
    private var depthNone: MTLDepthStencilState!
    private var meshes: [UInt32: GPUMesh] = [:]
    private var gridBuffer: MTLBuffer?
    private var gridVertexCount = 0
    private var gridSpacing: Float = -1
    private let sampleCount = 4
    private let colorFormat: MTLPixelFormat = .bgra8Unorm_srgb
    private let depthFormat: MTLPixelFormat = .depth32Float
    weak var model: SceneModel?
    private(set) var lastFrameTime: Double = 0
    private var frameStamp = CACurrentMediaTime()

    init?(view: MTKView, model: SceneModel) {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else { return nil }
        self.device = device
        self.queue = queue
        self.model = model
        super.init()
        view.device = device
        view.colorPixelFormat = colorFormat
        view.depthStencilPixelFormat = depthFormat
        view.sampleCount = sampleCount
        view.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        view.preferredFramesPerSecond = 60
        do { try buildPipelines() } catch {
            model.message = "Could not create the Metal pipelines: \(error.localizedDescription)"
            return nil
        }
    }

    private func buildPipelines() throws {
        guard let library = device.makeDefaultLibrary() else {
            throw NSError(domain: "SculptForge", code: 1, userInfo: [NSLocalizedDescriptionKey: "Metal shader library missing"])
        }
        func blended(_ d: MTLRenderPipelineDescriptor) {
            let a = d.colorAttachments[0]!
            a.pixelFormat = colorFormat
            a.isBlendingEnabled = true
            a.rgbBlendOperation = .add
            a.alphaBlendOperation = .add
            a.sourceRGBBlendFactor = .sourceAlpha
            a.destinationRGBBlendFactor = .oneMinusSourceAlpha
            a.sourceAlphaBlendFactor = .one
            a.destinationAlphaBlendFactor = .oneMinusSourceAlpha
            d.depthAttachmentPixelFormat = depthFormat
            d.rasterSampleCount = sampleCount
        }
        // mesh: six vertex streams, one per engine attribute array
        let vd = MTLVertexDescriptor()
        let formats: [MTLVertexFormat] = [.float3, .float3, .float3, .float, .float, .float]
        for i in 0..<6 {
            vd.attributes[i].format = formats[i]
            vd.attributes[i].offset = 0
            vd.attributes[i].bufferIndex = i
            vd.layouts[i].stride = GPUMesh.strides[i]
            vd.layouts[i].stepFunction = .perVertex
        }
        let md = MTLRenderPipelineDescriptor()
        md.vertexFunction = library.makeFunction(name: "mesh_vertex")
        md.fragmentFunction = library.makeFunction(name: "mesh_fragment")
        md.vertexDescriptor = vd
        blended(md)
        meshPipeline = try device.makeRenderPipelineState(descriptor: md)

        let bd = MTLRenderPipelineDescriptor()
        bd.vertexFunction = library.makeFunction(name: "background_vertex")
        bd.fragmentFunction = library.makeFunction(name: "background_fragment")
        blended(bd)
        backgroundPipeline = try device.makeRenderPipelineState(descriptor: bd)

        let lv = MTLVertexDescriptor()
        lv.attributes[0].format = .float3
        lv.attributes[0].offset = 0
        lv.attributes[0].bufferIndex = 0
        lv.attributes[1].format = .float4
        lv.attributes[1].offset = MemoryLayout<LineVertex>.offset(of: \LineVertex.color) ?? 16
        lv.attributes[1].bufferIndex = 0
        lv.layouts[0].stride = MemoryLayout<LineVertex>.stride
        let ld = MTLRenderPipelineDescriptor()
        ld.vertexFunction = library.makeFunction(name: "line_vertex")
        ld.fragmentFunction = library.makeFunction(name: "line_fragment")
        ld.vertexDescriptor = lv
        blended(ld)
        linePipeline = try device.makeRenderPipelineState(descriptor: ld)

        func depthState(compare: MTLCompareFunction, write: Bool) -> MTLDepthStencilState? {
            let d = MTLDepthStencilDescriptor()
            d.depthCompareFunction = compare
            d.isDepthWriteEnabled = write
            return device.makeDepthStencilState(descriptor: d)
        }
        depthWrite = depthState(compare: .lessEqual, write: true)
        depthRead = depthState(compare: .lessEqual, write: false)
        depthNone = depthState(compare: .always, write: false)
    }

    // MARK: - engine -> GPU synchronisation (main thread only)

    private func syncMeshes(_ model: SceneModel) {
        guard !model.busy else { return } // a background operation owns the engine right now
        var alive = Set<UInt32>()
        for obj in model.objects {
            alive.insert(obj.id)
            var view = SFMeshView()
            guard sf_object_mesh(model.scene, obj.id, &view) == 1 else { continue }
            let gpu = meshes[obj.id] ?? GPUMesh()
            meshes[obj.id] = gpu
            if gpu.topologyVersion != view.topologyVersion || gpu.vertexCount != Int(view.vertexCount) ||
                model.fullUpload.contains(obj.id) {
                gpu.uploadAll(view, device: device)
            } else if let range = model.dirtyRanges[obj.id] {
                gpu.uploadRange(view, first: Int(range.0), last: Int(range.1))
            }
        }
        model.fullUpload.removeAll()
        model.dirtyRanges.removeAll()
        for key in meshes.keys where !alive.contains(key) { meshes.removeValue(forKey: key) }
    }

    private func frameUniforms(_ model: SceneModel, camera: OrbitCamera) -> FrameUniforms {
        var u = FrameUniforms()
        let rs = model.render
        u.view = camera.viewMatrix
        u.viewProj = camera.projectionMatrix * camera.viewMatrix
        u.cameraPos = SIMD4<Float>(camera.position, camera.orthographic ? 1 : 0)
        var dirs = [SIMD4<Float>](repeating: SIMD4<Float>(0, 1, 0, 0), count: 4)
        var cols = [SIMD4<Float>](repeating: SIMD4<Float>(0, 0, 0, 0), count: 4)
        for (i, l) in rs.lights.prefix(4).enumerated() {
            let d = l.direction
            dirs[i] = l.isPoint ? SIMD4<Float>(camera.target + d * l.distance, 1) : SIMD4<Float>(d, 0)
            cols[i] = SIMD4<Float>(l.color * l.intensity, l.enabled ? 1 : 0)
        }
        u.lightDir0 = dirs[0]; u.lightDir1 = dirs[1]; u.lightDir2 = dirs[2]; u.lightDir3 = dirs[3]
        u.lightColor0 = cols[0]; u.lightColor1 = cols[1]; u.lightColor2 = cols[2]; u.lightColor3 = cols[3]
        u.ambientSky = SIMD4<Float>(rs.ambientSky, rs.ambientIntensity)
        u.ambientGround = SIMD4<Float>(rs.ambientGround, 0)
        u.params = SIMD4<Float>(rs.exposure, Float(rs.shading.rawValue), rs.showMask ? 1 : 0, model.maskOverlayOpacity)
        u.backgroundTop = SIMD4<Float>(rs.backgroundTop, 1)
        u.backgroundBottom = SIMD4<Float>(rs.backgroundBottom, 1)
        return u
    }

    private func rebuildGrid(spacing: Float) {
        var verts: [LineVertex] = []
        let n = 20
        let extent = Float(n) * spacing
        let c = SIMD4<Float>(1, 1, 1, 0.08), cMajor = SIMD4<Float>(1, 1, 1, 0.16)
        for i in -n...n {
            let p = Float(i) * spacing
            let col = i % 5 == 0 ? cMajor : c
            verts.append(LineVertex(position: SIMD3<Float>(p, 0, -extent), color: col))
            verts.append(LineVertex(position: SIMD3<Float>(p, 0, extent), color: col))
            verts.append(LineVertex(position: SIMD3<Float>(-extent, 0, p), color: col))
            verts.append(LineVertex(position: SIMD3<Float>(extent, 0, p), color: col))
        }
        // world axes: X red, Y green, Z blue
        let axes: [(SIMD3<Float>, SIMD4<Float>)] = [(SIMD3<Float>(1, 0, 0), SIMD4<Float>(0.9, 0.25, 0.25, 0.9)),
                                                    (SIMD3<Float>(0, 1, 0), SIMD4<Float>(0.3, 0.85, 0.3, 0.9)),
                                                    (SIMD3<Float>(0, 0, 1), SIMD4<Float>(0.3, 0.5, 1.0, 0.9))]
        for (dir, col) in axes {
            verts.append(LineVertex(position: SIMD3<Float>(0, 0, 0), color: col))
            verts.append(LineVertex(position: dir * extent * 0.25, color: col))
        }
        gridVertexCount = verts.count
        gridBuffer = device.makeBuffer(bytes: verts, length: verts.count * MemoryLayout<LineVertex>.stride, options: .storageModeShared)
        gridSpacing = spacing
    }

    /// Circle showing the brush footprint on the surface, plus mirrored copies when symmetry is on.
    private func cursorVertices(_ model: SceneModel) -> [LineVertex] {
        guard model.render.showBrushCursor, model.tool == .sculpt, let cur = model.cursor else { return [] }
        var out: [LineVertex] = []
        let n = simd_normalize(cur.normal)
        let helper: SIMD3<Float> = abs(n.y) < 0.9 ? SIMD3<Float>(0, 1, 0) : SIMD3<Float>(1, 0, 0)
        let t = simd_normalize(simd_cross(n, helper)), b = simd_cross(n, t)
        let r = model.brush.radius
        let col = SIMD4<Float>(1, 0.6, 0.15, 0.95)
        let segs = 48
        let lift = n * (r * 0.02)
        for i in 0..<segs {
            let a0 = Float(i) / Float(segs) * 2 * .pi, a1 = Float(i + 1) / Float(segs) * 2 * .pi
            out.append(LineVertex(position: cur.point + lift + (t * cos(a0) + b * sin(a0)) * r, color: col))
            out.append(LineVertex(position: cur.point + lift + (t * cos(a1) + b * sin(a1)) * r, color: col))
        }
        out.append(LineVertex(position: cur.point, color: col))
        out.append(LineVertex(position: cur.point + n * r * 0.5, color: col))
        return out
    }

    // MARK: - drawing

    private func encodeScene(_ enc: MTLRenderCommandEncoder, model: SceneModel, camera: OrbitCamera, overlays: Bool, background: Bool) {
        var frame = frameUniforms(model, camera: camera)
        enc.setVertexBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: 8)
        enc.setFragmentBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: 8)
        enc.setCullMode(.none)
        if background {
            enc.setRenderPipelineState(backgroundPipeline)
            enc.setDepthStencilState(depthNone)
            enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        }
        if overlays && model.render.showGrid {
            let spacing = pow(10, floor(log10(max(camera.distance, 0.001) / 4)))
            if gridBuffer == nil || spacing != gridSpacing { rebuildGrid(spacing: spacing) }
            if let gb = gridBuffer {
                enc.setRenderPipelineState(linePipeline)
                enc.setDepthStencilState(depthRead)
                enc.setVertexBuffer(gb, offset: 0, index: 0)
                enc.drawPrimitives(type: .line, vertexStart: 0, vertexCount: gridVertexCount)
            }
        }
        enc.setRenderPipelineState(meshPipeline)
        // opaque objects first, then transparent ones back to front without depth writes
        let visible = model.objects.filter { $0.props.visible != 0 && meshes[$0.id]?.indexCount ?? 0 > 0 }
        let opaque = visible.filter { $0.props.opacity >= 0.999 }
        let transparent = visible.filter { $0.props.opacity < 0.999 }.sorted {
            simd_length(SIMD3<Float>($0.world.columns.3.x, $0.world.columns.3.y, $0.world.columns.3.z) - camera.position) >
                simd_length(SIMD3<Float>($1.world.columns.3.x, $1.world.columns.3.y, $1.world.columns.3.z) - camera.position)
        }
        for (pass, list) in [opaque, transparent].enumerated() {
            enc.setDepthStencilState(pass == 0 ? depthWrite : depthRead)
            for obj in list {
                guard let gpu = meshes[obj.id], let ib = gpu.indexBuffer, gpu.streams.count == 6 else { continue }
                var u = ObjectUniforms()
                u.model = obj.world
                u.normalMatrix = obj.world.inverse.transpose
                u.baseColor = SIMD4<Float>(obj.props.baseColor.0, obj.props.baseColor.1, obj.props.baseColor.2, obj.props.opacity)
                u.material = SIMD4<Float>(obj.props.roughness, obj.props.metallic, obj.props.emission, obj.props.usePainted != 0 ? 1 : 0)
                u.flags = SIMD4<Float>(0, overlays && model.tool == .transform && obj.id == model.selection ? 1 : 0, 0, 0)
                for i in 0..<6 { enc.setVertexBuffer(gpu.streams[i], offset: 0, index: i) }
                enc.setVertexBytes(&u, length: MemoryLayout<ObjectUniforms>.stride, index: 9)
                enc.setFragmentBytes(&u, length: MemoryLayout<ObjectUniforms>.stride, index: 9)
                enc.setTriangleFillMode(.fill)
                enc.drawIndexedPrimitives(type: .triangle, indexCount: gpu.indexCount, indexType: .uint32, indexBuffer: ib, indexBufferOffset: 0)
                if overlays && model.render.showWireframe {
                    u.flags.x = 1
                    enc.setVertexBytes(&u, length: MemoryLayout<ObjectUniforms>.stride, index: 9)
                    enc.setFragmentBytes(&u, length: MemoryLayout<ObjectUniforms>.stride, index: 9)
                    enc.setTriangleFillMode(.lines)
                    enc.setDepthBias(-1, slopeScale: -1, clamp: 0)
                    enc.drawIndexedPrimitives(type: .triangle, indexCount: gpu.indexCount, indexType: .uint32, indexBuffer: ib, indexBufferOffset: 0)
                    enc.setDepthBias(0, slopeScale: 0, clamp: 0)
                    enc.setTriangleFillMode(.fill)
                }
            }
        }
        if overlays {
            let cursor = cursorVertices(model)
            if !cursor.isEmpty, let cb = device.makeBuffer(bytes: cursor, length: cursor.count * MemoryLayout<LineVertex>.stride, options: .storageModeShared) {
                enc.setRenderPipelineState(linePipeline)
                enc.setDepthStencilState(depthNone)
                enc.setVertexBuffer(cb, offset: 0, index: 0)
                enc.drawPrimitives(type: .line, vertexStart: 0, vertexCount: cursor.count)
            }
        }
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        model?.camera.aspect = Float(max(size.width, 1) / max(size.height, 1))
    }

    func draw(in view: MTKView) {
        if let v = view as? ViewportMTKView, let model = model { v.updateGizmoDisplay(model) }
        guard let model = model, let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
              let cmd = queue.makeCommandBuffer() else { return }
        let now = CACurrentMediaTime()
        lastFrameTime = now - frameStamp
        frameStamp = now
        model.frameSeconds = lastFrameTime
        syncMeshes(model)
        let size = view.drawableSize
        model.camera.aspect = Float(max(size.width, 1) / max(size.height, 1))
        guard let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }
        encodeScene(enc, model: model, camera: model.camera, overlays: true, background: true)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }

    /// Render the real scene off-screen at an arbitrary resolution and return it as an image.
    func renderImage(width: Int, height: Int, transparent: Bool) -> CGImage? {
        guard let model = model, width > 0, height > 0, width <= 8192, height <= 8192 else { return nil }
        syncMeshes(model)
        let msaa = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: colorFormat, width: width, height: height, mipmapped: false)
        msaa.textureType = .type2DMultisample
        msaa.sampleCount = sampleCount
        msaa.usage = .renderTarget
        msaa.storageMode = .private
        let resolve = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: colorFormat, width: width, height: height, mipmapped: false)
        resolve.usage = [.renderTarget, .shaderRead]
        resolve.storageMode = .shared
        let depth = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: depthFormat, width: width, height: height, mipmapped: false)
        depth.textureType = .type2DMultisample
        depth.sampleCount = sampleCount
        depth.usage = .renderTarget
        depth.storageMode = .private
        guard let colorTex = device.makeTexture(descriptor: msaa), let resolveTex = device.makeTexture(descriptor: resolve),
              let depthTex = device.makeTexture(descriptor: depth), let cmd = queue.makeCommandBuffer() else { return nil }
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = colorTex
        pass.colorAttachments[0].resolveTexture = resolveTex
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .multisampleResolve
        pass.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: transparent ? 0 : 1)
        pass.depthAttachment.texture = depthTex
        pass.depthAttachment.loadAction = .clear
        pass.depthAttachment.storeAction = .dontCare
        pass.depthAttachment.clearDepth = 1
        guard let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return nil }
        var camera = model.camera
        camera.aspect = Float(width) / Float(height)
        encodeScene(enc, model: model, camera: camera, overlays: false, background: !transparent)
        enc.endEncoding()
        cmd.commit()
        cmd.waitUntilCompleted()
        let rowBytes = width * 4
        var pixels = [UInt8](repeating: 0, count: rowBytes * height)
        resolveTex.getBytes(&pixels, bytesPerRow: rowBytes, from: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0)
        guard let provider = CGDataProvider(data: Data(pixels) as CFData),
              let space = CGColorSpace(name: CGColorSpace.sRGB) else { return nil }
        let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: rowBytes, space: space,
                       bitmapInfo: info, provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}
