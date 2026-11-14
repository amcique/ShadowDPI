# Genel (General)

Sağlayıcıdan bağımsız başlangıç tarifi. Superonline klasöründeki alternatifler
işe yaramazsa veya sağlayıcınız bilinmiyorsa buradan başlayın.

| No | Komut | Not |
| --- | --- | --- |
| 1 | `-5 --fake-ttl 5` ve DNS yönlendirme | DNS zehirlenmesi varsa ilk denenecek tarif |

## Kullanım

```text
console\1-dns-ttl5.cmd          konsolda çalıştır
service\install.cmd             hizmet olarak kur, tek tuşla 1 seç
service\install.cmd 1           hizmet olarak kur, doğrudan 1. tarif
service\remove.cmd              hizmeti kaldır
```

Ortak ayarlar ve DNS adresleri [../settings.cmd](../settings.cmd) dosyasındadır.
Ayrıntılar için [../README.md](../README.md) dosyasına bakın.

## English

Provider-independent starting recipe. Use it when the Superonline alternatives
do not work or the provider is unknown.

Recipe 1 applies `-5` with a fixed fake TTL of 5 and redirects DNS.

Console: `console\1-dns-ttl5.cmd`. Service: `service\install.cmd` and choose 1,
or run `service\install.cmd 1`. Remove with `service\remove.cmd`.

Shared settings and DNS endpoints are in [../settings.cmd](../settings.cmd).
See [../README.md](../README.md) for the full guide.
