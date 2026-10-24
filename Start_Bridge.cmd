@echo off
python "%~dp0scripts\launch_artifact_set.py" monaka %*
exit /b %ERRORLEVEL%
