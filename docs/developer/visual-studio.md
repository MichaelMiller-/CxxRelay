---
title: Visual Studio
---

# Develop CxxRelay with Visual Studio

**Visual Studio is the recommended workflow for developing CxxRelay on Windows.**
Open the CMake project directly, edit its settings in the IDE, and run the chat prototype without generating a separate `.sln` project.

<div class="grid cards" markdown>

-   :material-folder-open-outline: **Open the project**

    ---

    Visual Studio loads the root `CMakeLists.txt` and discovers its targets.

-   :material-tune: **Configure in the IDE**

    ---

    Select a configuration and edit the CMake cache variables.

-   :material-refresh-auto: **Automatic reconfiguration**

    ---

    Saving CMake configuration changes refreshes the build model.

-   :material-message-text-outline: **Run the native chat**

    ---

    Build the `prototype-powershell` custom target to launch Windows Forms.

</div>

## 1. Open and configure

1. Install Visual Studio with the **Desktop development with C++** workload and its CMake tools.
2. Choose **File → Open → Folder** and select the CxxRelay repository root, containing `CMakeLists.txt`.
3. Select a Windows x64 Debug configuration (or a suitable **Configure Preset**) in the toolbar.
4. Let Visual Studio complete the initial CMake configuration. Its **Output** window shows the result.

!!! tip "No solution generation required"
    CxxRelay is a CMake project. Visual Studio handles configuration, builds, IntelliSense, and CMake target discovery directly.

## 2. Change prototype settings

CxxRelay exposes the following CMake **cache variables**:

| Variable | Value | Purpose |
| --- | --- | --- |
| `CXXRELAY_PROTOTYPE_MODE` | `mock` / `openai` | Select the prototype provider; default: `mock` |
| `CXXRELAY_PROTOTYPE_OPENAI_API_KEY` | String, optional | API key override; prefer an environment variable |
| `CXXRELAY_PROTOTYPE_OPENAI_MODEL` | String | OpenAI model identifier, required in `openai` mode |

=== "Recommended: CMake Presets"

    Use `CMakePresets.json` for configurations shared by the team and `CMakeUserPresets.json` for local overrides. The following is an **optional example**, not a required replacement for your existing presets:

    ```json title="CMakePresets.json"
    {
      "version": 3,
      "configurePresets": [
        {
          "name": "windows-mock",
          "displayName": "Windows · Mock",
          "generator": "Ninja",
          "binaryDir": "${sourceDir}/out/build/${presetName}",
          "cacheVariables": {
            "CMAKE_BUILD_TYPE": "Debug",
            "BUILD_TESTING": "OFF",
            "CXXRELAY_PROTOTYPE_MODE": "mock"
          }
        }
      ]
    }
    ```

    Select the preset from Visual Studio's configuration dropdown. To use OpenAI, set `CXXRELAY_PROTOTYPE_MODE` to `openai`, specify `CXXRELAY_PROTOTYPE_OPENAI_MODEL`, and enable the project's optional HTTPS transport. Supply `OPENAI_API_KEY` through the environment.

=== "Alternative: CMake Settings"

    When the project uses `CMakeSettings.json`, choose **Manage Configurations** from the configuration dropdown or right-click `CMakeSettings.json` and select **Edit CMake Settings**.

    Edit the CMake variables for the active configuration and save the file. `CMakeSettings.json` and `CMakePresets.json` are **alternative** configuration mechanisms: Visual Studio does not combine them.

!!! warning "Keep API keys out of source control"
    Avoid placing real keys in `CMakePresets.json`, `CMakeSettings.json`, `CMakeUserPresets.json`, or `CMakeCache.txt`. The cache and generated build rules are not secret storage. Prefer the `OPENAI_API_KEY` environment variable, available to the Visual Studio process during configure **and** when running the custom target. Never commit local credentials.

## 3. Automatic CMake reload

When you save `CMakeLists.txt` or the active CMake configuration file, Visual Studio normally **reconfigures CMake automatically** and refreshes its target model. The same applies when switching to a different Configure Preset.

The updated cache, compiler settings, and target list then become available to the IDE.

!!! info "If a change is not picked up"
    Check **Tools → Options → CMake → General** to ensure automatic configuration is enabled. You can also invoke **Project → Configure Cache** (the exact label can vary with the Visual Studio version and presets mode). As a last resort, delete the CMake cache and reconfigure.

## 4. Build, run, and test

=== "Windows Forms prototype"

    1. Switch **Solution Explorer** to **CMake Targets View**.
    2. Locate `prototype-powershell`.
    3. Right-click the target and choose **Build**.
    4. The C++ worker is built as a dependency; the Windows Forms chat window then opens.

    **Start in `mock` mode first.** No Python installation, HTTP server, or API key is required.

=== "Browser prototype"

    Build the `prototype-python` target in **CMake Targets View**.
    Python 3 is required; then open <http://127.0.0.1:8765/>.

=== "Unit tests"

    Enable `BUILD_TESTING=ON` in the selected configuration. Reconfigure, build the tests, and run them using **Test → Run CTests** (or the equivalent test action in your Visual Studio version).

!!! note "Custom targets versus debugger targets"
    The `prototype-*` targets launch interactive applications via CMake custom commands. Choose **Build** to execute them. Use **F5** with an actual C++ executable target when you want to debug the worker itself.

## 5. Troubleshooting

| Symptom | Check |
| --- | --- |
| No prototype targets appear | Confirm the root `CMakeLists.txt` is open and CMake configuration succeeded |
| Configuration changes have no effect | Save the active preset/settings file; check the **Output** window and automatic configure options |
| OpenAI mode is rejected | Configure the model, API key environment, and optional HTTPS transport |
| Windows Forms does not launch | Use Windows PowerShell 5.1, check `prototype-powershell`, and inspect build output |
| Tests cannot configure | Install/discover Catch2 or use `BUILD_TESTING=OFF` for an initial prototype run |

For other platforms or script-based fallbacks, see [Chat Prototypes](chat-prototypes.md) and [Build & Test](build-and-test.md).

## Further reading

- [CMake projects in Visual Studio](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170)
- [Configure and build with CMake Presets](https://learn.microsoft.com/en-us/cpp/build/cmake-presets-vs?view=msvc-170)
- [Customize CMake build settings](https://learn.microsoft.com/en-us/cpp/build/customize-cmake-settings?view=msvc-170)
