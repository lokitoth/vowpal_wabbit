REM Integration points for toolchain customization
IF NOT DEFINED nugetPath (
    SET nugetPath=nuget
)

IF NOT DEFINED msbuildPath (
    REM Try to find VS Install
    FOR /f "usebackq tokens=*" %%i IN (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) DO (
        SET "InstallDir=%%i"
    )

    ECHO %InstallDir%

    IF NOT EXIST "%InstallDir%\MSBuild\15.0\Bin\MSBuild.exe" (
        ECHO ERROR: MsBuild couldn't be found
        EXIT /b 1
    ) ELSE (
        SET "msBuildPath=%InstallDir%\MSBuild\15.0\Bin\MSBuild.exe"
    )
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