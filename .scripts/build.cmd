@ECHO OFF
IF DEFINED DebugBuildScripts (
    @ECHO ON
)

SETLOCAL

CALL %~dp0init.cmd
PUSHD %~dp0

REM TODO: Figure out how to parametrize this script?! (is there a standard, or do we actually need parse args?)
ECHO Building "%vwRoot%\vowpalwabbit\vw.sln" for Release x64
"%msbuildPath%" "%vwRoot%\vowpalwabbit\vw.sln" /nr:false /v:normal /m /p:Configuration=Release;Platform=x64 

POPD

ENDLOCAL