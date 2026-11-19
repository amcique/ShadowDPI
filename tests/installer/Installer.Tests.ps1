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
            ($strategies | Where-Object { $_.Name -eq 'preset 9' }).Count | Should -Be 1
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
}
