Describe 'install.ps1 pure functions' {
    BeforeAll {
        $scriptPath = Join-Path $PSScriptRoot '..\..\install.ps1'
        . $scriptPath
    }

    It 'reads inline URLs and adds a scheme to bare hosts' {
        $urls = Read-UrlList -InlineUrls @('example.com', 'https://other.org/') -File ''
        $urls.Count | Should -Be 2
        $urls[0] | Should -Be 'https://example.com'
    }

    It 'ignores comments in a URL file' {
        $file = Join-Path ([IO.Path]::GetTempPath()) 'shadowdpi-url-test.txt'
        Set-Content -LiteralPath $file -Value @('# comment', '; another', 'example.com')
        try {
            $urls = Read-UrlList -InlineUrls $null -File $file
            $urls.Count | Should -Be 1
        }
        finally {
            Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
        }
    }

    It 'builds the preset strategies at every scan level' {
        foreach ($level in 'quick', 'standard', 'full') {
            $strategies = @(Get-Strategies -Level $level -DnsArgs @() -FakeSniHosts @())
            $strategies.Count | Should -BeGreaterThan 0
            @($strategies | Where-Object { $_.Name -eq 'preset 9' }).Count | Should -Be 1
        }
    }

    It 'maps probe variants' {
        (Get-ProbeVariants -Url 'https://example.com/' -ProtocolMode 'auto') | Should -Be 'https'
        @(Get-ProbeVariants -Url 'https://example.com/' -ProtocolMode 'all').Count | Should -Be 2
        (Get-ProbeVariants -Url 'http://example.com/' -ProtocolMode 'auto') | Should -Be 'http'
    }

    It 'uses safe defaults for install config' {
        $config = Get-InstallConfig -Metadata $null
        $config.ServiceName | Should -Be 'ShadowDPI'
        $config.ServiceArgs | Should -Be '-9'
        $config.ServiceEnabled | Should -BeFalse
    }

    It 'rejects protected install paths' {
        { Assert-SafeInstallPath -Path $env:SystemRoot } | Should -Throw
    }

    It 'rejects unsafe release manifest entries' {
        Test-ReleaseEntryPath -Entry 'docs/install.md' | Should -BeTrue
        Test-ReleaseEntryPath -Entry 'tools' | Should -BeTrue
        Test-ReleaseEntryPath -Entry '..\outside.txt' | Should -BeFalse
        Test-ReleaseEntryPath -Entry 'docs\..\outside.txt' | Should -BeFalse
        Test-ReleaseEntryPath -Entry 'C:\Windows\evil.txt' | Should -BeFalse
        Test-ReleaseEntryPath -Entry '\\server\share\evil.txt' | Should -BeFalse
        Test-ReleaseEntryPath -Entry 'docs\invalid:name.txt' | Should -BeFalse
    }

    It 'removes stale payload files from the install tree' {
        $root = Join-Path ([IO.Path]::GetTempPath()) ('shadowdpi-payload-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path (Join-Path $root 'docs') -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $root 'old.txt') -Value 'old'
        Set-Content -LiteralPath (Join-Path $root 'docs\old.md') -Value 'old'
        Set-Content -LiteralPath (Join-Path $root 'new.txt') -Value 'new'
        try {
            Remove-StalePayload -Path $root -OldPayload @('old.txt', 'docs\old.md') -NewPayload @('new.txt')
            Test-Path -LiteralPath (Join-Path $root 'old.txt') | Should -BeFalse
            Test-Path -LiteralPath (Join-Path $root 'docs\old.md') | Should -BeFalse
            Test-Path -LiteralPath (Join-Path $root 'new.txt') | Should -BeTrue
        }
        finally {
            Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    It 'registers and removes an Add/Remove Programs entry' {
        $root = 'Registry::HKEY_CURRENT_USER\Software\ShadowDPI-Installer-Test'
        $key = Join-Path $root $script:UninstallKeyName
        try {
            Set-UninstallRegistration -VersionText 'ShadowDPI 0.5.0 (C++20)' -InstallPath 'C:\Tools\ShadowDPI' -ScriptPath 'C:\Tools\ShadowDPI\install.ps1' -Root $root
            (Get-ItemProperty -LiteralPath $key).DisplayName | Should -Be 'ShadowDPI'
            (Get-ItemProperty -LiteralPath $key).DisplayVersion | Should -Be '0.5.0'
            (Get-ItemProperty -LiteralPath $key).QuietUninstallString | Should -Match '-Silent'
            Remove-UninstallRegistration -Root $root
            Test-Path -LiteralPath $key | Should -BeFalse
        }
        finally {
            Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    It 'verifies bundled driver hashes and rejects tampered files' {
        $root = Join-Path ([IO.Path]::GetTempPath()) ('shadowdpi-hash-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        $dll = Join-Path $root 'WinDivert.dll'
        $manifest = Join-Path $root 'release.sha256'
        try {
            Set-Content -LiteralPath $dll -Value 'original'
            $hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
            Set-Content -LiteralPath $manifest -Value "$hash  ./WinDivert.dll"
            Test-FileHashMatch -Path $dll -HashFile $manifest -LeafName 'WinDivert.dll' | Should -BeTrue

            Set-Content -LiteralPath $dll -Value 'tampered'
            Test-FileHashMatch -Path $dll -HashFile $manifest -LeafName 'WinDivert.dll' | Should -BeFalse

            # A manifest that exists but omits the file must fail closed.
            Set-Content -LiteralPath $manifest -Value "$hash  ./other.dll"
            Test-FileHashMatch -Path $dll -HashFile $manifest -LeafName 'WinDivert.dll' | Should -BeFalse

            # No manifest at all is the documented source-build case: skip with
            # a warning rather than failing.
            Test-FileHashMatch -Path $dll -HashFile '' -LeafName 'WinDivert.dll' | Should -BeTrue
        }
        finally {
            Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    It 'creates an install log at the requested path' {
        $log = Join-Path ([IO.Path]::GetTempPath()) ('shadowdpi-log-' + [Guid]::NewGuid().ToString('N') + '.log')
        try {
            Initialize-InstallLog -Path $log
            Test-Path -LiteralPath $log | Should -BeTrue
            (Get-Content -LiteralPath $log -Raw) | Should -Match 'installer started'
        }
        finally {
            $script:LogFile = ''
            Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
        }
    }
}
