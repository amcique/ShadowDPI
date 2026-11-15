<p align="center">
  <img src="resources/logo.png" alt="ShadowDPI logo" width="130">
</p>

<h1 align="center">ShadowDPI</h1>

<p align="center">
  <strong>Windows x64 için modern ve hafif DPI atlatma aracı.</strong>
</p>

<p align="center">
  <a href="https://github.com/amcique/ShadowDPI/actions/workflows/build.yml"><img src="https://github.com/amcique/ShadowDPI/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
  <a href="https://github.com/amcique/ShadowDPI/releases"><img src="https://img.shields.io/github/v/release/amcique/ShadowDPI?color=0078D6" alt="Latest release"></a>
  <a href="https://github.com/amcique/ShadowDPI/releases"><img src="https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows11&logoColor=white" alt="Windows x64"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue" alt="GPLv3"></a>
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.ru.md">Русский</a> · <strong>Türkçe</strong>
</p>

ShadowDPI, bir bağlantının ilk paketlerini değiştirerek derin paket incelemesinin
(DPI) bağlantıyı tanımasını zorlaştırır. WinDivert üzerinde normal bir program
olarak çalışır; bu yüzden trafiğiniz şifreli kalır ve uygulamalar gerçek sunucuya
bağlanmaya devam eder.

HTTP ve HTTPS ile çalışır; QUIC engellenerek tarayıcının TCP'ye geçmesi
sağlanabilir. Yerleşik strateji tarayıcısı, seçtiğiniz sitelerde farklı ayarları
dener. Yönetici hakları gerekir, çünkü bir çekirdek sürücüsü yükler. Bu bir VPN
değildir: IP adresiniz değişmez ve trafiğiniz başka bir sunucudan geçmez.

## Hızlı başlangıç

1. [Son sürümü](https://github.com/amcique/ShadowDPI/releases) indirin.
2. Arşivi herhangi bir klasöre çıkarın. Pakette `install.ps1`, `WinDivert.dll` ve
   `WinDivert64.sys` birlikte gelir.
3. Yönetici olarak açtığınız PowerShell'de, arşivi çıkardığınız klasörden:

   ```powershell
   .\install.ps1 -Action install
   ```

Bu komut, dosyaları `%ProgramFiles%\ShadowDPI` klasörüne kopyalar, `-9` ile
otomatik başlayan `ShadowDPI` hizmetini kaydeder ve başlatır. `-Action install
-NoService` yalnızca dosyaları kopyalar, hizmet oluşturmaz.

Kurmadan denemek için çalıştırılabilir dosyayı yönetici olarak başlatın.
Bağımsız değişken verilmezse `-9` uygulanır; çoğu sağlayıcı için doğru ilk deneme
budur:

```powershell
.\shadowdpi.exe
```

Sağlayıcı DNS yanıtlarını zehirliyorsa tarayıcıda güvenli DNS'i açın veya DNS'i
paket düzeyinde yönlendirin:

```powershell
.\shadowdpi.exe -9 --dns-addr 77.88.8.8 --dns-port 1253
```

Yükseltmek için yeni sürümü çıkarın ve o klasörde `.\install.ps1 -Action
upgrade` çalıştırın; mevcut hizmet ayarları korunur. Hizmet bağımsız
değişkenlerini değiştirmek veya hizmeti kaldırmak için `-Action reconfigure` ve
`-Action uninstall` kullanın. Elle güncellerken `shadowdpi.exe`,
`WinDivert.dll` ve `WinDivert64.sys` dosyalarını birlikte değiştirin, böylece
sürümleri uyumlu kalsın.

## Neden ShadowDPI

- **Açık bırakılabilecek kadar hafif.** 19 MiB/s hızındaki yoğun bir bağlantıda
  tek çekirdeğin yaklaşık %0,03'ü. GoodbyeDPI ile tam karşılaştırma
  [Performans](#performans) bölümünde.
- **Yapısı gereği şeffaf.** ShadowDPI durursa uygulamalar çalışmaya devam eder;
  sertifika deposuna hiçbir şey kurulmaz ve proxy ayarları değişmez.
- **Yalnızca hazır ayarlar değil, strateji tarayıcısı da var.** DPI sağlayıcıya
  göre değişir ve zamanla güncellenir. `install.ps1 -Action scan` hazır ayarları
  ve seçenek birleşimlerini kendi URL listenizde dener, engellenen her adresi
  açan ve çalışan hiçbir adresi bozmayan ilk stratejiyi bildirir.
- **Modern C++20, sistem yazılımı gibi yazıldı.** Tek bir çalıştırılabilir dosya
  ve paketle gelen WinDivert sürücüsü; platformdan bağımsız ve birim testleri olan
  bir çekirdek, ince bir Windows katmanı, `WinDivertHelperParsePacket` ile bayt
  bayt karşılaştırılan ve rastgele bir korpusla sınanan bir paket ayrıştırıcı,
  CI'da hız eşiği denetlenen bir ayrıştırıcı ölçümü.
- **İncelemesi açık.** İlk herkese açık sürümden önce 20 bulgu içeren bir kaynak
  incelemesi yapıldı ve hepsi düzeltildi. Rapor:
  [`docs/reviews/2026-11-source-review.md`](docs/reviews/2026-11-source-review.md).

## Karşılaştırma

| | ShadowDPI | GoodbyeDPI | Zapret | ByeDPI |
|---|---|---|---|---|
| Platform | Windows x64 | Windows | Linux, Windows (`winws`) | Platformdan bağımsız |
| Yöntem | WinDivert, şeffaf | WinDivert, şeffaf | netfilter / WinDivert desync | Yerel SOCKS5 proxy |
| Yönetici gerekir | Evet | Evet | Evet | Hayır |
| Yapılandırma | Hazır ayarlar, yaklaşık 40 anahtar | Hazır ayarlar, anahtarlar | Yapılandırma dosyaları ve host listeleri | Anahtarlar ve proxy istemcisi |
| Yerleşik strateji tarayıcısı | Evet | Hayır | `blockcheck` | Hayır |
| Ölçülen GiB başına CPU | **1× (temel)** | 67,8× | ölçülmedi | ölçülmedi |

ShadowDPI ile GoodbyeDPI doğrudan karşılaştırıldı; Zapret ve ByeDPI
karşılaştırılmadı ve "ölçülmedi" bir üstünlük iddiası değildir. Aynı sorunu
farklı ödünleşmelerle çözerler ve Zapret, ShadowDPI'den çok daha fazla desync
seçeneği sunar. Hazır ayarlar sağlayıcınıza uymuyorsa onları da deneyin. Yöntem
ve uyarılar: [docs/performance.md](docs/performance.md).

## Ne yapıyor

- **Pasif DPI'yi engeller.** Enjekte edilen yönlendirmeler ve TCP sıfırlamaları
  uygulamaya ulaşmadan düşürülür.
- **Aktif DPI'yi atlatır.** HTTP istekleri ve TLS ClientHello paketleri bölünür,
  yeniden sıralanır ve yeniden yazılır; isteğe bağlı sahte paketler kısa bir TTL,
  hatalı bir sağlama toplamı veya eski bir sıra numarası taşır.
- **QUIC ile baş eder.** `-9`, QUIC/HTTP3 Initial paketlerini düşürür ve
  tarayıcının TCP'ye dönmesini sağlar; diğer teknikler orada uygulanır.
- **Kapsamı daraltır.** Kara liste, host adına bakan tekniklerin yalnızca seçilen
  alan adlarında uygulanmasını sağlar. SYN/ACK penceresi adımı host adı
  bilinmeden çalıştığı için işlenen portlarda genel kalmak zorundadır.
- **Zehirlenmiş DNS'i yönlendirir.** IPv4 ve IPv6 çözümleyici adresleri uçuşta
  değiştirilebilir; işlem kimliği, soru ve çözümleyici adresi doğrulanır.

## Performans

Aynı makinede, aynı iş yükleri, aynı bayt sayıları ve sırayla değişen turlarla
GoodbyeDPI v0.2.3rc3 ile karşılaştırıldığında ShadowDPI **gigabayt başına 67,8
kat daha az CPU** kullandı ve aktarım süresi değişmedi.

![CPU cost per GiB](docs/performance/cpu-per-gib.svg)

Görünen tutarsızlık aslında sonucun kendisi: iki sürüm de veriyi aynı hızda
taşıyor, ancak ShadowDPI bunu çok daha az çevrimle yapıyor. Bu, makineyi paylaşan
yönlendiricilere, VPN'lere ve diğer yazılımlara alan bırakır.

![Wall-clock parity](docs/performance/wall-clock-parity.svg)

A/A kontrolü aynı ikiliyi iki yuvada çalıştırır. Medyan oran 1,193, aralık 0,766
ile 1,547 arasındaydı; yani bu makinede yüzde onlarca fark normaldir. HTTP, HTTPS
ve keep-alive için A/B medyanlarının tamamı bu aralığa düşer; eşleştirilmiş CPU
oranları (0,0023, 0,0011, 0,0911) ise bu aralığa girmez.

![CPU cycles by workload](docs/performance/cpu-cycles.svg)

On iki iş yükünün tamamı, gecikme dağılımları ve uyarılar:
[docs/performance.md](docs/performance.md).

## Hazır ayarlar

Hazır ayarlar birbirine eklenen seçenek gruplarıdır; sonraki bağımsız
değişkenler öncekileri geçersiz kılar.

| Hazır ayar | Ne ekler |
| --- | --- |
| `-1` ile `-4` arası | Pasif engelleme ve HTTP Host teknikleri; TLS bölme 2, 40 veya kapalı |
| `-5` | HTTP/TLS bölme, ters sıra ve otomatik sahte TTL |
| `-6` | Bölme ve yanlış TCP sıra numarası taşıyan sahte paketler |
| `-7` | Bölme ve hatalı sağlama toplamı taşıyan sahte paketler |
| `-8` | Hem yanlış sıra numarası hem hatalı sağlama toplamı |
| `-9` | `-8` artı QUIC engelleme (varsayılan) |

Kullanışlı varyasyonlar:

```powershell
.\shadowdpi.exe -5                                  # -9 çok agresifse
.\shadowdpi.exe -5 --blacklist C:\blocked.txt       # yalnızca listedeki alan adları
.\shadowdpi.exe -9 --verbose                        # ayrıntılı günlük
.\shadowdpi.exe --help                              # bu derlemenin seçenekleri
```

`-9` QUIC'i bilinçli olarak engeller. Bir uygulama QUIC'e ihtiyaç duyuyor ve
TCP'ye dönemiyorsa `-5` ile başlayın.

### Çalışan stratejiyi bulma

Hazır ayarlar bir başlangıç noktasıdır, garanti değildir. Strateji tarayıcısı
hazır ayarları ve seçenek birleşimlerini bir URL listesi üzerinde dener; zapret'in
blockcheck aracıyla aynı temel karşılaştırmayı kullanır. Her aday için
ayrı bir ShadowDPI süreci başlatır, `--ready-file` dosyasını bekler, URL'leri
`curl.exe` ile yoklar ve engellenen her adresi açan, çalışan bir adresi
bozmayan ilk stratejiyi bildirir.

```powershell
.\install.ps1 -Action scan -ScanLevel quick
.\install.ps1 -Action scan -UrlFile .\blocked.txt -ScanLevel standard -Json result.json
.\install.ps1 -Action scan -Urls wikipedia.org,x.com -DnsAddr 77.88.8.8 -DnsPort 1253
```

Tarama `install.ps1` menüsünde de bulunur. `tools\scan-urls.txt` dosyasını
düzenleyin veya `-Urls` ile kendi listenizi verin. Etkileşimli oturumda tarayıcı
hangi DNS çözümleyicisinin kullanılacağını sorar: Cloudflare, Google, başka bir
çözümleyici veya sistem çözümleyicisi. `-DnsResolver system` sorusunu atlar,
`-DnsAddr` ise özel adres ve port verir. Tarayıcı yönetici haklarına ve
`curl.exe` dosyasına ihtiyaç duyar; temel ölçüm için kurulu ve çalışan
hizmeti durdurur, sonra yeniden başlatır. Tarama düzeyleri, sonuç değerlendirmeleri,
JSON alanları ve çıkış kodları [docs/scan.md](docs/scan.md) dosyasında.

Seçeneklerin tam başvurusu, takma ad tablosu, kara liste biçimi ve doğrulama
kuralları [docs/cli.md](docs/cli.md) dosyasında. Kurulum eylemleri, hizmet
ayarları ve `install.json` için [docs/install.md](docs/install.md).

## Gereksinimler

- **Windows 10 veya 11, x64.** Windows 7 ve 8.1, sistem SHA-2 sürücü imzalarına
  güveniyorsa çalışabilir
  ([KB3033929](https://www.microsoft.com/en-us/download/details.aspx?id=46148)),
  ancak birincil test hedefi değildir.
- **Yönetici hakları.** Kurulum bir hizmet kaydeder ve WinDivert sürücüsü ilk
  çalıştırmada yüklenir.
- **Kendi kendine yeten çalıştırılabilir dosya.** Statik MSVC çalışma zamanı
  kullanılır; sürüm paketi uyumlu WinDivert DLL ve sürücüsünü içerir.
  `install.ps1` çalıştırmak zorunlu değildir, yalnızca dosyaları kopyalar ve
  hizmeti kaydeder.
- **PowerShell 5.1** `install.ps1` için gereklidir. Strateji tarayıcısı ayrıca
  `curl.exe` ister. İkisi de Windows 10 ve sonrasında gelir.

## Sınırlamalar

- **Evrensel bir hazır ayar yok.** DPI davranışı sağlayıcıya göre değişir ve
  zamanla güncellenir. `-9` bir başlangıç noktasıdır, garanti değildir. Hazır
  ayarlar çalışmıyorsa kendi URL listenizde
  [strateji tarayıcısını](docs/scan.md) çalıştırın.
- **IPv6 uygulandı ama sahada doğrulanmadı.** Ölçüm makinesinde canlı bir IPv6 DPI
  yolu yoktu, bu yüzden IPv6 atlatma gerçek bir sağlayıcıda sınanmadı.
- **QUIC engelleme davranışı değiştirir.** `-9` bilinçli olarak TCP'ye dönmeye
  zorlar.
- **Eski Windows 7 imajları** SHA-2 desteği kurulu değilse WinDivert sürücüsünü
  reddedebilir.
- **Killer/Intel ağ kartları:** ShadowDPI başlamıyor veya takılıyorsa Killer
  Control Center içinde *Advanced Stream Detect* seçeneğini kapatın.
- **Ölçüm sayıları makineye bağlıdır.** Göreli CPU avantajı sağlamdır, mutlak
  hızlar sizin bağlantınız için bir vaat değildir.
- **Yalnızca Windows x64.** ARM64, 32 bit, Linux ve macOS derlemesi yoktur.

## İndirmeyi doğrulama

Her sürüm `shadowdpi-<sürüm>-x64.zip` dosyasını ve eşleşen `.sha256` dosyasını
yayımlar:

```powershell
Get-FileHash .\shadowdpi-0.3.0-x64.zip -Algorithm SHA256
Get-Content .\shadowdpi-0.3.0-x64.sha256 | Select-String 'shadowdpi-0.3.0-x64.zip'
```

Arşiv ayrıca GitHub derleme kaynağı kanıtı ile imzalanır; bu kayıt arşivi hangi
iş akışının, hangi commit'in ve hangi çalıştırıcının ürettiğini gösterir:

```powershell
gh attestation verify .\shadowdpi-0.3.0-x64.zip --repo amcique/ShadowDPI
```

İki denetimden biri başarısız olursa ikiliyi çalıştırmayın.

Çalıştırılabilir dosya Authenticode ile imzalanmadığı için Windows "Windows
protected your PC" uyarısı gösterebilir ve bazı antivirüsler uyarabilir: çekirdek
sürücü kuran imzasız bir dosya için bu beklenen bir durumdur. Karmayı ve
kanıtı doğrulayın veya kaynaktan derleyin.

## Amaçlanan kullanım

ShadowDPI ağ araştırması, hakkınız olan bilgilere erişim ve DPI cihazlarının
nasıl davrandığını anlama amacıyla geliştirildi. Ağlara saldırmak için bir araç
değildir. Yasalar ve ağ politikaları ülkeye ve kuruma göre değişir; aracı yasal
ve kurallara uygun biçimde kullanmak sizin sorumluluğunuzdadır ve bu metin
hukuki danışmanlık değildir.

## Lisans

ShadowDPI, [GNU General Public License, version 3](LICENSE) altında
yayımlanmıştır. Her sürümün tam kaynak kodu bu depodaki eşleşen etikettir.
WinDivert değiştirilmeden LGPLv3 altında dağıtılır; ayrıntılar [NOTICE](NOTICE)
ve [third_party/WinDivert/LICENSE](third_party/WinDivert/LICENSE) dosyalarında.
