// SculptForge 3D - application entry point
import SwiftUI

@main
struct SculptForgeApp: App {
    var body: some Scene {
        WindowGroup {
            RootView()
                .statusBarHidden(true)
                .persistentSystemOverlays(.hidden)
        }
    }
}
