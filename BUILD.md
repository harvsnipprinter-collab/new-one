# Building SculptForge 3D

## 1. Engine and tests (any OS; verified on Windows 11 with LLVM-MinGW clang 23)

```
bash Scripts/build_engine.sh
./build/sf_tests build/test_output
node Tools/validate_exports.mjs build/test_output
./build/sf_bench build/bench_output
```

The validator needs Node 22 or newer. `CXX=g++ bash Scripts/build_engine.sh` selects another compiler.
Any C++20 compiler should work; only clang with libc++ has actually been tried.

## 2. Build the IPA without a Mac (GitHub's hosted Mac)

iOS apps can only be compiled by Xcode on macOS. This project contains a workflow that does it on a
GitHub-hosted Mac. **It has not been run yet, and the Swift and Metal code has never been compiled**, so
the first run may stop on compile errors. The log is uploaded as the `build-logs` artifact either way.

1. Create a GitHub account and a new **private** repository.
2. Upload the contents of this folder to it, including the hidden `.github` folder.
3. Open the repository's **Actions** tab. The "Build iOS app" workflow starts on every push; you can also
   start it with **Run workflow**.
4. When it finishes, open the run and download the **SculptForge-unsigned-ipa** artifact (a zip that
   contains `SculptForge-unsigned.ipa`).
5. Open that IPA in Sideloadly, sign in with your Apple ID and install it on your device. Sideloadly
   signs the app with your own Apple developer identity. With a free Apple ID the install expires after
   7 days and has to be refreshed.

What the workflow does: builds and runs the engine tests on the Mac, generates the Xcode project with
XcodeGen, builds for the iOS Simulator, launches the app in a simulator and saves a screenshot
(`simulator-launch.png`), archives a Release build for devices without signing, and packages it as an IPA.

If the build fails, download `build-logs` and look at `simulator-build.log`: the lines containing
`error:` name the file and line to fix.

## 3. Build on your own Mac

```
brew install xcodegen
xcodegen generate
open SculptForge.xcodeproj
```

In Xcode select the SculptForge target, choose your team under Signing & Capabilities, change the bundle
identifier `com.sculptforge.app` to one of your own, then Run on a device, or Product > Archive >
Distribute App to export a signed IPA. Command line equivalent for an unsigned archive:

```
xcodebuild archive -project SculptForge.xcodeproj -scheme SculptForge -configuration Release -sdk iphoneos -destination "generic/platform=iOS" -archivePath build/SculptForge.xcarchive CODE_SIGNING_ALLOWED=NO
bash Scripts/make_ipa.sh build/SculptForge.xcarchive build/SculptForge-unsigned.ipa
```

## Requirements

* iOS / iPadOS 16.0 or later and a Metal-capable device.
* Xcode 15 or later (the workflow uses the Xcode preinstalled on the `macos-15` runner).
* No third-party libraries: everything is in this project.
