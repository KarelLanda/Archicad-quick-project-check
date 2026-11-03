import os
import sys
import platform
import subprocess
import shutil
import codecs
from pathlib import Path

def RunResConv(resConvPath, platformSign, codepage, inputFilePath, nativeResourceFilePath, resourcesFolders, colorChangeScriptPath):
    buildcommand = [
        str(resConvPath),
        '-m', 'r',
        '-T', platformSign,
        '-q', 'utf8', codepage,
        '-w', '2',
        '-p', ';'.join([str(p) for p in resourcesFolders]),
        '-i', str(inputFilePath),
        '-o', str(nativeResourceFilePath),
        '-py', sys.executable,
        '-sc', str(colorChangeScriptPath),
    ]
    return subprocess.call(buildcommand) == 0

def PrecompileGRCMac(clangArgs, inputFilePath, precompiledGrcFilePath):
    cmd = ['clang', '-x', 'c++', '-E', '-P', '-Dmacintosh'] + clangArgs + ['-o', str(precompiledGrcFilePath), str(inputFilePath)]
    return subprocess.call(cmd) == 0

def PrecompileGRCWin(incPath, dglibPath, sourcesPath, inputFilePath, precompiledGrcFilePath):
    cmd = [
        'cl',
        '/nologo',
        '/X',
        '/EP',
        '/P',
        f'/I{incPath}',
        f'/I{dglibPath}',
        f'/I{sourcesPath}',
        '/DWINDOWS',
        '/utf-8',
        f'/Fi{precompiledGrcFilePath}',
        str(inputFilePath)
    ]
    return subprocess.call(cmd) == 0

def Main():
    if len(sys.argv) < 7:
        print("Usage: CompileResourcesAC.py <languageCode> <devKitPath> <sourcesPath> <resourcesPath> <resourceObjectsPath> <resultResourcePath>")
        return 1

    languageCode = sys.argv[1]
    devKitPath = Path(sys.argv[2]).resolve()
    sourcesPath = Path(sys.argv[3]).resolve()
    resourcesPath = Path(sys.argv[4]).resolve()
    resourceObjectsPath = Path(sys.argv[5]).resolve()
    resultResourcePath = Path(sys.argv[6]).resolve()

    isWindows = (platform.system() == 'Windows')

    if isWindows:
        toolsWinPath = devKitPath / 'Tools' / 'Win'
        if not toolsWinPath.exists():
            toolsWinPath = devKitPath / 'Support' / 'Tools' / 'Win'
        resConvPath = toolsWinPath / 'ResConv.exe'
        colorChangeScriptPath = toolsWinPath / 'SVGColorChange.py'
    else:
        toolsOSXPath = devKitPath / 'Tools' / 'OSX'
        if not toolsOSXPath.exists():
            toolsOSXPath = devKitPath / 'Support' / 'Tools' / 'OSX'
        resConvPath = toolsOSXPath / 'ResConv'
        colorChangeScriptPath = toolsOSXPath / 'SVGColorChange.py'

    incPath = devKitPath / 'Inc'
    if not incPath.exists():
        incPath = devKitPath / 'Support' / 'Inc'

    modulesPath = devKitPath / 'Modules'
    if not modulesPath.exists():
        modulesPath = devKitPath / 'Support' / 'Modules'

    dglibPath = modulesPath / 'DGLib'

    resourcesFolders = [
        resourcesPath / 'RFIX' / 'Images',
        resourcesPath / 'RFIX'
    ]

    clangArgs = [
        f'-I{incPath}',
        f'-I{dglibPath}',
        f'-I{sourcesPath}',
        f'-I{resourceObjectsPath}'
    ]

    resourceObjectsPath.mkdir(parents=True, exist_ok=True)

    locFolder = resourcesPath / f'R{languageCode}'
    if locFolder.exists():
        for grcFile in locFolder.glob('*.grc'):
            precompiled = resourceObjectsPath / f'{grcFile.name}.i'
            if isWindows:
                if not PrecompileGRCWin(incPath, dglibPath, sourcesPath, grcFile, precompiled):
                    print(f"Precompilation failed: {grcFile}")
                    return 1
                roFile = resourceObjectsPath / f'{grcFile.name}.rc2'
                if not RunResConv(resConvPath, 'W', 'utf8', precompiled, roFile, resourcesFolders, colorChangeScriptPath):
                    print(f"ResConv failed: {grcFile}")
                    return 1
            else:
                if not PrecompileGRCMac(clangArgs, grcFile, precompiled):
                    print(f"Precompilation failed: {grcFile}")
                    return 1
                roFile = resourceObjectsPath / f'{grcFile.stem}.ro'
                if not RunResConv(resConvPath, 'M', 'utf16', precompiled, roFile, resourcesFolders, colorChangeScriptPath):
                    print(f"ResConv failed: {grcFile}")
                    return 1

    fixFolder = resourcesPath / 'RFIX'
    if fixFolder.exists():
        for grcFile in fixFolder.glob('*.grc'):
            precompiled = resourceObjectsPath / f'{grcFile.name}.i'
            if isWindows:
                if not PrecompileGRCWin(incPath, dglibPath, sourcesPath, grcFile, precompiled):
                    print(f"Precompilation failed: {grcFile}")
                    return 1
                roFile = resourceObjectsPath / f'{grcFile.name}.rc2'
                if not RunResConv(resConvPath, 'W', 'utf8', precompiled, roFile, resourcesFolders, colorChangeScriptPath):
                    print(f"ResConv failed: {grcFile}")
                    return 1
            else:
                if not PrecompileGRCMac(clangArgs, grcFile, precompiled):
                    print(f"Precompilation failed: {grcFile}")
                    return 1
                roFile = resourceObjectsPath / f'{grcFile.stem}.ro'
                if not RunResConv(resConvPath, 'M', 'utf16', precompiled, roFile, resourcesFolders, colorChangeScriptPath):
                    print(f"ResConv failed: {grcFile}")
                    return 1

    if isWindows:
        winFolder = resourcesPath / 'RFIX.win'
        rc2Files = list(winFolder.glob('*.rc2'))
        if not rc2Files:
            print("Native resource file (.rc2) not found in RFIX.win")
            return 1
        rcCmd = [
            'rc',
            '/c65001',
            f'/i{incPath}',
            f'/i{dglibPath}',
            f'/i{sourcesPath}',
            f'/i{resourceObjectsPath}',
            f'/fo{resultResourcePath}',
            str(rc2Files[0])
        ]
        if subprocess.call(rcCmd) != 0:
            print("Native RC compilation failed")
            return 1
    else:
        localizedResPath = resultResourcePath / 'English.lproj'
        localizedResPath.mkdir(parents=True, exist_ok=True)
        stringsPath = localizedResPath / 'Localizable.strings'
        with codecs.open(str(stringsPath), 'w', 'utf-16') as stringsOut:
            for item in resourceObjectsPath.iterdir():
                ext = item.suffix.lower()
                if ext == '.tif':
                    shutil.copy(item, resultResourcePath)
                elif ext == '.rsrd':
                    shutil.copy(item, localizedResPath)
                elif ext == '.strings':
                    with codecs.open(str(item), 'r', 'utf-16') as stringsIn:
                        stringsOut.write(stringsIn.read())

    return 0

if __name__ == '__main__':
    sys.exit(Main())
