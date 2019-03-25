@ECHO OFF
IF DEFINED DebugBuildScripts (
    @ECHO ON
)

SETLOCAL

CALL %~dp0init.cmd
PUSHD %~dp0

REM TODO: Figure out how to parametrize this script?! (is there a standard, or do we actually need parse args?)

REM ECHO Running make_config to propagate version
REM PUSHD "%vwRoot%\vowpalwabbit"

REM REM This is idempotent, and for some reason is not triggering from the build...
REM "win32\make_config_h.exe"

REM POPD

ECHO Building "%vwRoot%\vowpalwabbit\vw.sln" for Release x64
"%msbuildPath%" "%vwRoot%\vowpalwabbit\vw.sln" /nr:false /v:normal /m /p:Configuration=Release /p:Platform=x64

POPD

ENDLOCAL