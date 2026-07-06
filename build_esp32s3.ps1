Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$toolchain  = 'C:/Users/user/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin'
$cmake      = 'C:/Users/user/.espressif/tools/cmake/3.30.2/bin'
$ninja      = 'C:/Users/user/.espressif/tools/ninja/1.12.1'
$python_env = 'C:/Users/user/.espressif/python_env/idf5.5_py3.11_env/Scripts'
$env:PATH   = "$toolchain;$cmake;$ninja;$python_env;" + $env:PATH
$env:IDF_PATH = 'D:/esp/v5.5/esp-idf'
$env:IDF_TOOLS_PATH = 'C:/Users/user/.espressif'
$idf    = 'C:/Users/user/.espressif/python_env/idf5.5_py3.11_env/Scripts/python.exe'
$idfpy  = 'D:/esp/v5.5/esp-idf/tools/idf.py'
Set-Location D:/GitHub/MoTrotten
& $idf $idfpy build 2>&1
