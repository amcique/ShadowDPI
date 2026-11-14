# Türkiye presetleri

Bu klasör, Türkiye'deki servis sağlayıcılarında sık görülen DPI
davranışlarına yönelik altı hazır komut birleşimi içerir.

Betikler `shadowdpi.exe` dosyasını çağırır; uygulamanın kendisinde ülkeye özel
bir kod yoktur. Tüm ayarlar [settings.cmd](settings.cmd) dosyasında toplanmıştır,
böylece DNS adreslerini değiştirmek için tek bir yeri düzenlemeniz yeterlidir.

> Betikler yönetici hakları ister. Bir betiğe sağ tıklayıp
> **Yönetici olarak çalıştır** seçeneğini kullanın.

## Klasör düzeni

```text
presets/turkiye/
  settings.cmd        ortak ayarlar ve altı komut satırı
  console/            tek seferlik çalıştırma, konsol penceresi açık kalır
    1-dns-ttl5.cmd
    2-fragment.cmd
    3-dns-ttl3.cmd
    4-fragment-dns.cmd
    5-full-dns.cmd
    6-full.cmd
    _run.cmd          konsol betiklerinin ortak çalıştırıcısı
  service/            Windows hizmeti olarak kurulum
    install.cmd
    remove.cmd
```

## Altı tarif

| No | Komut | Ne zaman kullanılır |
| --- | --- | --- |
| 1 | `-5 --fake-ttl 5` ve DNS yönlendirme | DNS zehirlenmesi varsa ilk denenecek tarif |
| 2 | `-5` | DNS'i Windows ayarlarından veya tarayıcıdan değiştiriyorsanız |
| 3 | `--fake-ttl 3` ve DNS yönlendirme | 1 işe yaramazsa |
| 4 | `-5` ve DNS yönlendirme | TTL'i değiştirmeden DNS yönlendirme |
| 5 | `-9` ve DNS yönlendirme | En agresif tarif, DNS dahil |
| 6 | `-9` | En agresif tarif, DNS hariç |

1, 3, 4 ve 5 numaralı tarifler DNS'i paket düzeyinde yönlendirir. 2 ve 6
numaralı tarifler DNS'e dokunmaz; bu durumda DNS'i Windows ayarlarından veya
tarayıcıdan kendiniz değiştirmelisiniz.

`-5`, `-9` ve `--fake-ttl` seçeneklerinin ne yaptığı
[../../docs/cli.md](../../docs/cli.md) dosyasında ayrıntılı olarak anlatılmıştır.

## Konsol

`console` klasöründeki bir betiği çift tıklayın. ShadowDPI ön planda çalışır,
konsol penceresi açık kalır ve çıktıyı görebilirsiniz. Durdurmak için pencereyi
kapatın veya Ctrl+C basın. Ardından tarayıcıyı yeniden başlatmak gerekebilir.

## Windows hizmeti

`service\install.cmd` dosyasını yönetici olarak çalıştırın. Betik size numara
sorar:

```text
Preset number [1-6]: 5
```

Aynı işlemi komut satırından da yapabilirsiniz:

```cmd
service\install.cmd 5
```

Hizmet `ShadowDPI` adıyla kurulur ve bilgisayar açılışında otomatik başlar.
Hizmeti kaldırmak için `service\remove.cmd` dosyasını yönetici olarak
çalıştırın. Kaldırma işlemi WinDivert sürücü hizmetlerine dokunmaz, çünkü bu
hizmetler başka araçlarla paylaşılır.

## DNS ayarları

[settings.cmd](settings.cmd) dosyasının başındaki değerleri düzenleyin:

```bat
set "SDP_DNS4=77.88.8.8"
set "SDP_DNS4_PORT=1253"
set "SDP_DNS6=2a02:6b8::feed:0ff"
set "SDP_DNS6_PORT=1253"
```

- Varsayılan adresler Yandex DNS'e aittir. Erişilebilen başka bir çözümleyici
  kullanabilirsiniz.
- `SDP_DNS6` boş bırakılırsa IPv6 yönlendirmesi kapatılır.
- `SDP_DNS4` boş bırakılmamalıdır; DNS kullanan tarifler bu adrese ihtiyaç duyar.
- Bağlantı noktası `0` olamaz.

`settings.cmd` içindeki `SDP_EXE` değişkeni varsayılan olarak `presets`
klasörünün iki üstündeki `shadowdpi.exe` dosyasını gösterir. Derleme çıktısını
başka bir yerden çalıştırıyorsanız bir launcher çağırmadan önce `SDP_EXE`
değişkenini kendiniz tanımlayabilirsiniz.

## Sorun giderme

- **"shadowdpi.exe was not found"**: `shadowdpi.exe`, `WinDivert.dll` ve
  `WinDivert64.sys` dosyalarının `presets` klasörüyle aynı dizinde olduğundan
  emin olun.
- **Hizmet başlamıyor**: `sc query ShadowDPI` çalıştırın ve Windows Olay
  Görüntüleyicisi'ndeki uygulama günlüğüne bakın.
- **Hiçbir tarif işe yaramıyor**: tarayıcıda güvenli DNS'i kapatıp tekrar
  deneyin; bazı sağlayıcılar 53 dışındaki DNS bağlantı noktalarını da engeller.
- **QUIC**: `-9` QUIC trafiğini bilerek engeller ve TCP'ye geri dönüş ister.
  Bir uygulama QUIC olmadan çalışmıyorsa `-5` ile başlayın.

## English

This folder ships six ready-made recipes for DPI behavior commonly seen with
Turkish providers. The application stays generic; the scripts only collect
flags and DNS endpoints.

Requirements:

- Run the scripts as Administrator.
- Keep `shadowdpi.exe`, `WinDivert.dll`, and `WinDivert64.sys` in the folder
  two levels above `presets\turkiye`, as in the release archive.

Console scripts live in `console\`. Double-click one and leave the window open
while ShadowDPI runs. Service scripts live in `service\`;
`service\install.cmd` prompts for a preset number, or accepts one as an
argument, for example `service\install.cmd 5`. `service\remove.cmd` removes the
`ShadowDPI` service and leaves the shared WinDivert services alone.

All DNS values are variables at the top of `settings.cmd`. Leave `SDP_DNS6`
empty to disable the IPv6 redirect. Each port requires its matching address
and must be greater than zero.

| No | Command | Use when |
| --- | --- | --- |
| 1 | `-5 --fake-ttl 5` plus DNS redirect | DNS is poisoned; first choice |
| 2 | `-5` | DNS is changed in Windows settings or in the browser |
| 3 | `--fake-ttl 3` plus DNS redirect | Recipe 1 does not work |
| 4 | `-5` plus DNS redirect | DNS redirect without changing the TTL |
| 5 | `-9` plus DNS redirect | Most aggressive, DNS included |
| 6 | `-9` | Most aggressive, DNS excluded |

Recipes 1, 3, 4, and 5 redirect DNS at the packet layer. Recipes 2 and 6 leave
DNS alone, so change it in Windows settings or in the browser yourself.

The flag combinations follow the community recipes that circulated for
GoodbyeDPI. The scripts and their messages are written from scratch for
ShadowDPI.
