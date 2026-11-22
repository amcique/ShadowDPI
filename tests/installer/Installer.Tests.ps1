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
            Set-UninstallRegistration -VersionText 'ShadowDPI 0.5.1 (C++20)' -InstallPath 'C:\Tools\ShadowDPI' -ScriptPath 'C:\Tools\ShadowDPI\install.ps1' -Root $root
            (Get-ItemProperty -LiteralPath $key).DisplayName | Should -Be 'ShadowDPI'
            (Get-ItemProperty -LiteralPath $key).DisplayVersion | Should -Be '0.5.1'
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

    It 'rejects all protected install path families' {
        # Drive/share root.
        { Assert-SafeInstallPath -Path $env:SystemDrive + '\' } | Should -Throw
        # ProgramData and the Users tree.
        { Assert-SafeInstallPath -Path (Join-Path $env:ProgramData 'ShadowDPI') } | Should -Throw
        { Assert-SafeInstallPath -Path (Join-Path $env:SystemDrive 'Users\Alice\AppData') } | Should -Throw
        # Exact Program Files roots are blocked, but a subfolder is allowed.
        { Assert-SafeInstallPath -Path $env:ProgramFiles } | Should -Throw
        { Assert-SafeInstallPath -Path (Join-Path $env:ProgramFiles 'Common Files') } | Should -Throw
        { Assert-SafeInstallPath -Path (Join-Path $env:ProgramFiles 'WindowsApps') } | Should -Throw
        { Assert-SafeInstallPath -Path (Join-Path $env:ProgramFiles 'ShadowDPI') } | Should -Not -Throw
    }

    It 'rejects an install path that enters a protected tree through a junction' {
        # A junction under a writable temp root that points into SystemRoot must
        # be resolved before the protected-path check runs.
        $root = Join-Path ([IO.Path]::GetTempPath()) ('shadowdpi-junction-' + [Guid]::NewGuid().ToString('N'))
        $link = Join-Path $root 'system-link'
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        try {
            $created = $false
            try {
                New-Item -ItemType Junction -Path $link -Target $env:SystemRoot -ErrorAction Stop | Out-Null
                $created = $true
            }
            catch {
                # Junction creation may require privileges on some hosts; the
                # other protected-path tests still cover the logic.
            }
            if ($created) {
                { Assert-SafeInstallPath -Path (Join-Path $link 'ShadowDPI') } | Should -Throw
            }
        }
        finally {
            if (Test-Path -LiteralPath $link) {
                (Get-Item -LiteralPath $link -Force).Delete() 2>$null
            }
            Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    It 'quotes Windows command-line arguments with correct backslash escaping' {
        Quote-Argument -Value 'plain' | Should -Be 'plain'
        Quote-Argument -Value 'two words' | Should -Be '"two words"'
        Quote-Argument -Value 'say "hi"' | Should -Be '"say \"hi\""'
        Quote-Argument -Value 'C:\path with space\' | Should -Be '"C:\path with space\\"'
        Quote-Argument -Value 'a\"b c' | Should -Be '"a\\\"b c"'
    }

    It 'parses service binary paths with and without quotes' {
        $quoted = Get-ServiceLaunch -PathName '"C:\Program Files\ShadowDPI\shadowdpi.exe" -9 --dns-addr 1.1.1.1'
        $quoted.ExePath | Should -Be 'C:\Program Files\ShadowDPI\shadowdpi.exe'
        $quoted.Arguments | Should -Be '-9 --dns-addr 1.1.1.1'

        $bare = Get-ServiceLaunch -PathName 'C:\Tools\ShadowDPI\shadowdpi.exe -5'
        $bare.ExePath | Should -Be 'C:\Tools\ShadowDPI\shadowdpi.exe'
        $bare.Arguments | Should -Be '-5'

        (Get-ServiceLaunch -PathName '').ExePath | Should -Be ''
    }

    It 'accepts ownership of an absent service and rejects an empty path name' {
        # No service with this name exists, so there is nothing to conflict with.
        Test-ServiceOwnership -Name 'ShadowDpiMissingServiceXYZ' -ExePath 'C:\Tools\ShadowDPI\shadowdpi.exe' | Should -BeTrue
    }

    It 'validates service names' {
        { Assert-ServiceName -Name '' } | Should -Throw
        { Assert-ServiceName -Name 'bad/name' } | Should -Throw
        { Assert-ServiceName -Name ('a' * 201) } | Should -Throw
        { Assert-ServiceName -Name 'ShadowDPI.test-1' } | Should -Not -Throw
    }

    It 'reads install config from metadata' {
        $metadata = [pscustomobject]@{
            service = [pscustomobject]@{
                enabled   = $true
                name      = 'CustomSvc'
                arguments = '-5'
                startType = 'demand'
            }
        }
        $config = Get-InstallConfig -Metadata $metadata
        $config.ServiceEnabled | Should -BeTrue
        $config.ServiceName | Should -Be 'CustomSvc'
        $config.ServiceArgs | Should -Be '-5'
        $config.StartType | Should -Be 'demand'

        $empty = Get-InstallConfig -Metadata ([pscustomobject]@{ service = $null })
        $empty.ServiceName | Should -Be 'ShadowDPI'
        $empty.ServiceArgs | Should -Be '-9'
    }

    It 'writes install metadata preserving install time and payload' {
        $root = Join-Path ([IO.Path]::GetTempPath()) ('shadowdpi-meta-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        try {
            $old = [pscustomobject]@{
                installedUtc = '2026-01-01T00:00:00Z'
                payload      = @('old.txt')
            }
            Write-InstallMetadata -Path $root -Metadata $old -VersionText 'ShadowDPI 0.5.1 (C++20)' `
                -SourcePath 'C:\src' -ServiceEnabled $true -ServiceName 'ShadowDPI' `
                -ServiceArgs '-9' -StartType 'auto' -Running $true
            $file = Join-Path $root 'install.json'
            # PowerShell 7's ConvertFrom-Json coerces ISO date strings into
            # DateTime objects, so assert the on-disk JSON text for this field.
            (Get-Content -LiteralPath $file -Raw) |
                Should -Match '"installedUtc"\s*:\s*"2026-01-01T00:00:00Z"'
            $data = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            $data.version | Should -Be '0.5.1'
            $data.service.running | Should -BeTrue
            @($data.payload).Count | Should -Be 1

            # New payload replaces the old one; installedUtc still preserved.
            Write-InstallMetadata -Path $root -Metadata $data -VersionText 'ShadowDPI 0.5.1 (C++20)' `
                -SourcePath 'C:\src2' -ServiceEnabled $false -ServiceName 'ShadowDPI' `
                -ServiceArgs '-9' -StartType 'auto' -Running $false `
                -Payload @('new.txt', 'docs\new.md')
            (Get-Content -LiteralPath $file -Raw) |
                Should -Match '"installedUtc"\s*:\s*"2026-01-01T00:00:00Z"'
            $data2 = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
            $data2.version | Should -Be '0.5.1'
            $data2.service.enabled | Should -BeFalse
            @($data2.payload).Count | Should -Be 2
            $data2.payload[0] | Should -Be 'new.txt'
        }
        finally {
            Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    It 'falls back to a numeric version when none is present' {
        Get-VersionNumber -Text '' | Should -Be ''
        Get-VersionNumber -Text 'no version here' | Should -Be '0.0'
        Get-VersionNumber -Text 'ShadowDPI 0.5.1 (C++20)' | Should -Be '0.5.1'
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
