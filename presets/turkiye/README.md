# Türkiye presetleri

Bu klasör, Türkiye'deki servis sağlayıcılarında sık görülen DPI
davranışlarına yönelik hazır komut birleşimlerini sağlayıcıya göre gruplar.
Betikler yalnızca `shadowdpi.exe` dosyasını çağırır; uygulamanın kendisinde
ülkeye veya sağlayıcıya özel kod yoktur.

> Betikler yönetici hakları ister. Bir betiğe sağ tıklayıp
> **Yönetici olarak çalıştır** seçeneğini kullanın.

## Sağlayıcı grupları

| Klasör | İçerik |
| --- | --- |
| [genel](genel/README.md) | Sağlayıcıdan bağımsız başlangıç tarifi |
| [superonline](superonline/README.md) | Superonline için bildirilen beş alternatif |

Yeni bir sağlayıcı eklemek için `superonline` klasörünü kopyalayın, adını
değiştirin ve `settings.cmd` içindeki tarifleri düzenleyin. Bu tabloya da bir
satır ekleyin.

## Klasör düzeni

```text
presets/turkiye/
  settings.cmd          ortak: shadowdpi.exe yolu, DNS adresleri, DNS argümanları
  run.cmd               konsol launcher'larının ortak çalıştırıcısı
  install-service.cmd   hizmet kurulumunun ortak betiği
  remove-service.cmd    hizmet kaldırmanın ortak betiği
  genel/
    settings.cmd        bu grubun tarifleri
    README.md
    console/            1-dns-ttl5.cmd
    service/            install.cmd, remove.cmd
  superonline/
    settings.cmd        bu grubun tarifleri
    README.md
    console/            1-fragment.cmd ... 5-full.cmd
    service/            install.cmd, remove.cmd
```

Her sağlayıcı klasörü kendi `settings.cmd` dosyasında `SDP_COUNT`,
`SDP_LABEL_n` ve `SDP_ARGS_n` değerlerini tanımlar. Tarif numaraları her grupta
1'den başlar.

## Ortak ayarlar

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

`SDP_EXE`, varsayılan olarak `presets` klasörünün iki üstündeki
`shadowdpi.exe` dosyasını gösterir. Başka bir konum kullanmak için bir
launcher çağırmadan önce bu değişkeni tanımlayın.

## Konsol ve hizmet

- `genel\console\` veya `superonline\console\` içindeki bir betiği çift
  tıklayın. ShadowDPI ön planda çalışır; durdurmak için Ctrl+C basın.
- `...\service\install.cmd` dosyasını yönetici olarak çalıştırın. Betik o
  grubun tariflerini listeler ve tek tuşla seçim ister. Komut satırından
  `install.cmd 3` biçiminde de kullanılabilir.
- `...\service\remove.cmd` yalnızca `ShadowDPI` hizmetini kaldırır. WinDivert
  sürücü hizmetleri paylaşıldığı için dokunulmaz.

Aynı anda tek bir `ShadowDPI` hizmeti kurulabilir. Farklı bir tarif kurmak
mevcut hizmeti değiştirir.

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

This folder groups ready-made DPI recipes by provider. The scripts only call
`shadowdpi.exe`; the application itself has no country or provider specific
code.

| Folder | Contents |
| --- | --- |
| [genel](genel/README.md) | Provider-independent starting recipe |
| [superonline](superonline/README.md) | Five alternatives reported for Superonline |

Shared files:

- `settings.cmd`: executable path and DNS endpoints, used by every group.
- `run.cmd`: console runner.
- `install-service.cmd` and `remove-service.cmd`: service management.
- Each provider folder defines `SDP_COUNT`, `SDP_LABEL_n`, and `SDP_ARGS_n`,
  with recipe numbers starting at 1.

Run the scripts as Administrator. Console launchers keep the window open while
ShadowDPI runs. `service\install.cmd` lists that provider's recipes and takes a
single keypress, or accepts a number such as `install.cmd 3`. Only one
`ShadowDPI` service can exist at a time; installing another recipe replaces it.

All DNS values are variables at the top of `settings.cmd`. Leave `SDP_DNS6`
empty to disable the IPv6 redirect. Each port requires its matching address and
must be greater than zero.

The flag combinations follow the community recipes that circulated for
GoodbyeDPI. The scripts and their messages are written from scratch for
ShadowDPI.
