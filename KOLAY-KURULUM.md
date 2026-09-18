# Tek tıkla `.exe` kurulum nasıl çıkar

Cevap: **hiçbir şey kurmadan, GitHub'a derletiyoruz.**

Mantık şu: o `.exe` sihirbaz aslında bir kutu. İçinde zaten derlenmiş
dosyalar var. Birinin bir yerde derlemesi şart — ama bu "biri" sen olmak
zorunda değilsin. GitHub, her depoya ücretsiz bir Windows bilgisayar veriyor.
O bilgisayar derliyor, paketliyor, `.exe`'yi sana hazır veriyor.

Senin bilgisayarına Visual Studio, CMake, hiçbiri kurulmuyor.

---

## Adım adım

### 1. GitHub hesabı aç
https://github.com/signup — ücretsiz, 2 dakika.

### 2. Yeni depo oluştur
Sağ üstte **+** → **New repository**

- **Repository name:** `obs-depth-bokeh`
- **Public** seç (ücretsiz derleme sadece public depolarda sınırsız)
- **Create repository**'ye bas

### 3. Dosyaları yükle

Açılan sayfada **"uploading an existing file"** linkine tıkla.

Sana verdiğim `obs-depth-bokeh` klasörünün **içindeki her şeyi** sürükleyip
bırak. Klasörü değil, içindekileri.

> **Dikkat:** `.github` klasörü nokta ile başladığı için Windows'ta gizli
> olabilir. Dosya Gezgini'nde **Görünüm → Gizli öğeler** kutusunu işaretle,
> yoksa bu klasörü sürükleyemezsin — ve o klasör olmadan hiçbir şey çalışmaz.

Aşağıdaki **Commit changes** butonuna bas.

### 4. Bekle

Üstteki **Actions** sekmesine tıkla. Sarı bir nokta göreceksin — derleme
başladı.

**İlk sefer 20–25 dakika sürer.** Sonraki seferler 3–4 dakikaya iner (OBS
kısmı önbelleğe alınıyor). Sekmeyi kapatabilirsin, arka planda devam eder.

Yeşil tik çıktığında bitti.

### 5. `.exe`'yi indir

Actions sekmesinde yeşil tikli satıra tıkla. Sayfanın en altında
**Artifacts** bölümünde `obs-depth-bokeh-installer` yazan bir dosya var.
Tıkla, iner.

İçinden çıkan `.exe`'yi çift tıkla. Klasik kurulum sihirbazı açılır:
Türkçe, İleri-İleri-Kur. OBS klasörünü kendi buluyor, derinlik modelini de
içinde getiriyor.

Kurulum bitince OBS'i aç → webcam → Filtreler → **Depth Bokeh**.
Model dosyası seçmene bile gerek yok, hazır geliyor.

---

## Başkalarına dağıtmak istersen

Deponda **Releases** → **Create a new release** → tag olarak `v1.0.0` yaz →
**Publish**.

Derleme bitince `.exe` otomatik olarak o sayfaya düşer. Artık herkese tek
link verebilirsin — tam olarak obs-backgroundremoval'ın yaptığı şey.

---

## Kırmızı çarpı çıkarsa

Bu ilk denemede olabilir, panik yok.

1. **Actions** → kırmızı çarpılı satıra tıkla
2. Kırmızı çarpılı **adıma** tıkla (hangi aşamada patladığını gösterir)
3. Oradaki hata metnini kopyalayıp bana at

En sık iki sebep:

| Belirti | Sebep |
|---|---|
| `Build libobs` adımında patlar | OBS sürümü ile bağımlılık paketi uyuşmamıştır — `build-windows.yml` içindeki `OBS_VERSION` değerini değiştirmek çözer |
| `Fetch ONNX Runtime` adımında 404 | O sürüm kaldırılmıştır — `ORT_VERSION` değerini güncel bir sürümle değiştir |

İkisi de dosyanın en üstündeki iki satır, tek kelime değişiyor.

---

## Neden kendi bilgisayarında derlemek yerine bu?

| | Kendi bilgisayarın | GitHub Actions |
|---|---|---|
| Kurulacak program | ~10 GB (VS, CMake, Git) | Yok |
| İlk süre | 1–2 saat, bol hata | 25 dk, sen bakmıyorsun |
| Sonraki derlemeler | Her seferinde uğraş | Dosyayı değiştir, otomatik |
| Başkasına verilebilir mi | Elle paketlemen gerek | Otomatik `.exe` |
| Maliyet | — | Ücretsiz |
