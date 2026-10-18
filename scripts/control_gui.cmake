# Explicit normal product source/reference set. No PowerShell policy bypass.
set(MB_CONTROL_SOURCES
 ui/control_models.cs ui/installation_control.cs ui/steamvr_control_gui_main.cs
 ui/steamvr_control_wpf.cs ui/steamvr_control_tray.cs
 ui/discovery_models.cs ui/discovery_panel.cs ui/discovery_window.cs
 ui/calibration_json.cs ui/calibration_profile.cs ui/calibration_models.cs
 ui/calibration_owned_process.cs ui/calibration_pipe_frames.cs
 ui/calibration_process_client.cs ui/calibration_panel.cs ui/calibration_profile_dialog.cs)
list(TRANSFORM MB_CONTROL_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
set(MB_CONTROL_NATIVE_SOURCES)
foreach(source IN LISTS MB_CONTROL_SOURCES)
 file(TO_NATIVE_PATH "${source}" native_source)
 list(APPEND MB_CONTROL_NATIVE_SOURCES "${native_source}")
endforeach()
find_program(MB_CSC NAMES csc.exe PATHS "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319" NO_DEFAULT_PATH REQUIRED)
set(MB_NET48_REFERENCES "$ENV{ProgramFiles\(x86\)}/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8" CACHE PATH ".NET Framework 4.8 WPF reference assemblies")
set(MB_CONTROL_REFERENCES /reference:System.dll /reference:System.Core.dll /reference:System.Web.Extensions.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll)
foreach(assembly WindowsBase PresentationCore PresentationFramework System.Xaml)
 if(NOT EXISTS "${MB_NET48_REFERENCES}/${assembly}.dll")
  message(FATAL_ERROR "Normal Bridge WPF build requires .NET 4.8 references: ${assembly}.dll")
 endif()
 list(APPEND MB_CONTROL_REFERENCES "/reference:${MB_NET48_REFERENCES}/${assembly}.dll")
endforeach()
set(MB_CONTROL_EXE "${CMAKE_CURRENT_BINARY_DIR}/control/$<CONFIG>/monaka_bridge_control.exe")
add_custom_command(OUTPUT "${MB_CONTROL_EXE}"
 COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/control/$<CONFIG>"
 COMMAND "${MB_CSC}" /nologo /utf8output /target:winexe /optimize+ "/out:${MB_CONTROL_EXE}" ${MB_CONTROL_REFERENCES} ${MB_CONTROL_NATIVE_SOURCES}
 DEPENDS ${MB_CONTROL_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/control_gui.cmake" VERBATIM)
add_custom_target(monaka_bridge_control ALL DEPENDS "${MB_CONTROL_EXE}")
if(BUILD_TESTING AND TARGET calibration_runtime_process_fixture)
 foreach(group json profile models job launch client wpf b4)
  add_test(NAME calibration_gui_${group} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_calibration_managed.py"
   --group "${group}" --root "${CMAKE_CURRENT_SOURCE_DIR}" --out "${CMAKE_CURRENT_BINARY_DIR}/managed-${group}"
   --compiler "${MB_CSC}" --references "${MB_NET48_REFERENCES}"
   --native "$<TARGET_FILE:calibration_runtime_process_fixture>" --config "$<TARGET_FILE:monaka_bridge_config>")
  set_tests_properties(calibration_gui_${group} PROPERTIES TIMEOUT 240)
 endforeach()
endif()
