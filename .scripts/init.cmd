REM Integration points for toolchain customization
IF NOT DEFINED nugetPath (
    SET nugetPath=nuget
)

IF NOT DEFINED msbuildPath (
    SET msbuildPath=msbuild
)

IF NOT DEFINED vstestPath (
    SET vstestPath=vstest.console
)

REM Repo-specific paths
IF NOT DEFINED vwRoot (
    SET vwRoot=%~dp0..
)

IF NOT DEFINED Version (
    SET Version=8.6.1
)

IF NOT DEFINED VowpalWabbitAssemblyVersion (
    SET VowpalWabbitAssemblyVersion=%Version%
)

IF NOT DEFINED Tag (
    SET Tag=-INTERNALONLY
)