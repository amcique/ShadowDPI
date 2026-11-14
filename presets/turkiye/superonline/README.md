# Superonline

Superonline bağlantılarında bildirilen beş tarif. Numaralar bu gruba özeldir.

| No | Komut | Ne zaman |
| --- | --- | --- |
| 1 | `-5` | DNS'i Windows ayarlarından veya tarayıcıdan değiştiriyorsanız |
| 2 | `--fake-ttl 3` ve DNS yönlendirme | 1 işe yaramazsa |
| 3 | `-5` ve DNS yönlendirme | DNS yönlendirme, TTL değişmeden |
| 4 | `-9` ve DNS yönlendirme | En agresif, DNS dahil |
| 5 | `-9` | En agresif, DNS hariç |

1 ve 5 numaralı tarifler DNS'e dokunmaz; bu durumda DNS'i Windows ayarlarından
veya tarayıcıdan kendiniz değiştirmelisiniz. 2, 3 ve 4 numaralı tarifler DNS'i
paket düzeyinde yönlendirir. Bu beş tarif işe yaramazsa
[genel](../genel/README.md) klasöründeki tarifi deneyin.

## Kullanım

```text
console\1-fragment.cmd ... console\5-full.cmd
service\install.cmd             hizmet olarak kur, tek tuşla seç
service\install.cmd 4           hizmet olarak kur, doğrudan 4. tarif
service\remove.cmd              hizmeti kaldır
```

Ortak ayarlar ve DNS adresleri [../settings.cmd](../settings.cmd) dosyasındadır.
Ayrıntılar için [../README.md](../README.md) dosyasına bakın.

## English

Five recipes reported for Superonline. Numbers are local to this group.

| No | Command | Use when |
| --- | --- | --- |
| 1 | `-5` | DNS is changed in Windows settings or in the browser |
| 2 | `--fake-ttl 3` plus DNS redirect | Recipe 1 does not work |
| 3 | `-5` plus DNS redirect | DNS redirect without changing the TTL |
| 4 | `-9` plus DNS redirect | Most aggressive, DNS included |
| 5 | `-9` | Most aggressive, DNS excluded |

Recipes 1 and 5 leave DNS alone, so change it yourself in Windows settings or
in the browser. Recipes 2, 3, and 4 redirect DNS at the packet layer. If none of
these work, try the recipe in [genel](../genel/README.md).

Console: `console\1-fragment.cmd` through `console\5-full.cmd`. Service:
`service\install.cmd` and choose a number, or run `service\install.cmd 4`.
Remove with `service\remove.cmd`.

Shared settings and DNS endpoints are in [../settings.cmd](../settings.cmd).
See [../README.md](../README.md) for the full guide.
