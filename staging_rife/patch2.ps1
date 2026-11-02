$ErrorActionPreference = 'Stop'
$cpp = 'C:\Users\dd841\.cache\lsfg-build\LSFG-Android-Application\app\src\main\cpp'
$utf8 = New-Object System.Text.UTF8Encoding($false)

# ---- fix 1+2: restore android_shader_loader.hpp includes (kOk error codes) ----
$targets = @(
    @{ File = Join-Path $cpp 'lsfg_render_loop.cpp'; Anchor = '#include "cpu_postprocess.hpp"' },
    @{ File = Join-Path $cpp 'android_vk_probe.cpp';  Anchor = '#include "android_vk_probe.hpp"' }
)
foreach ($t in $targets) {
    $text = [IO.File]::ReadAllText($t.File)
    if ($text.Contains('android_shader_loader.hpp')) {
        Write-Output ("skip (already patched): " + $t.File)
        continue
    }
    $eol = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    if (-not $text.Contains($t.Anchor)) { throw ("anchor not found in " + $t.File) }
    $text = $text.Replace($t.Anchor, $t.Anchor + $eol + '#include "android_shader_loader.hpp"')
    [IO.File]::WriteAllText($t.File, $text, $utf8)
    Write-Output ("patched: " + $t.File)
}

# ---- fix 3: framegen_impl.cpp -- volk.h first, then neutralize simplevk.h ----
$f = Join-Path $cpp 'rife_framegen\framegen_impl.cpp'
$lines = [System.Collections.Generic.List[string]]([IO.File]::ReadAllLines($f))
if ($lines[0] -ne '#include "framegen_impl.hpp"' -or $lines[3] -ne '#include <volk.h>') {
    throw ('unexpected framegen_impl.cpp prologue: [' + $lines[0] + '] [' + $lines[3] + ']')
}
$head = [string[]]@(
    '#include <volk.h>'
    ''
    '#ifdef __ANDROID__'
    '#include <android/hardware_buffer.h>'
    '#include <vulkan/vulkan_android.h>'
    '#endif'
    ''
    '// ncnn prebuilts hard-define NCNN_SIMPLEVK=1 in platform.h, which would pull'
    '// in simplevk.h and redefine every Vulkan type volk.h already provided via'
    '// the NDK headers above. Pre-defining simplevk.h''s include guard makes that'
    '// include a no-op, so the ncnn headers transparently reuse the NDK type'
    '// definitions (both sides are ABI-identical: same handle macros, same enum'
    '// and struct layouts). This only affects the present translation unit.'
    '#define NCNN_SIMPLEVK_H'
    ''
    '#include "framegen_impl.hpp"'
    '#include "rife_net.hpp"'
)
$lines.RemoveRange(0, 9)
$lines.InsertRange(0, $head)
[IO.File]::WriteAllLines($f, $lines, $utf8)
Write-Output ('patched: ' + $f)
