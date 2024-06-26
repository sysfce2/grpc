@rem Copyright 2024 gRPC authors.
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem     http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem Avoid slow finalization after the script has exited.
@rem See the script's prologue for info on the correct invocation pattern.
setlocal EnableDelayedExpansion
IF "%cd%"=="T:\src" (
  call %~dp0\..\..\..\tools\internal_ci\helper_scripts\move_src_tree_and_respawn_itself.bat %0
  echo respawn script has finished with exitcode !errorlevel!
  exit /b !errorlevel!
)
endlocal

@rem enter repo root
cd /d %~dp0\..\..\..

echo "!TIME!: Preparing for the Windows build"
call tools/internal_ci/helper_scripts/prepare_build_windows.bat || exit /b 1

@rem Install clang-cl
echo "!TIME!: Installing llvm"
choco install -y llvm --version=18.1.6
set BAZEL_LLVM="C:\Program Files\LLVM"
clang-cl --version

@rem Install bazel
@rem Side effect of the tools/bazel script is that it downloads the correct version of bazel binary.
echo "!TIME!: Installing bazel"
mkdir C:\bazel
bash -c "tools/bazel --version && cp tools/bazel-*.exe /c/bazel/bazel.exe"
set PATH=C:\bazel;%PATH%
bazel --version

echo "!TIME!: Running the bazel build"
bazel build --config=clang-cl --config=windows_opt --build_tag_filters=-no_windows :all src/core:all

echo "!TIME!: Job Finished"
