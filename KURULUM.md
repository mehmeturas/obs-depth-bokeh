# Depth Bokeh — OBS Studio Eklentisi

Webcam arkaplanını **derinliğe göre kademeli** bulanıklaştıran bir OBS filtresi.
Klasik arkaplan silme eklentilerinden farkı: insanı kesip arkayı düz bulanık
yapmıyor. Modelden bir derinlik haritası alıyor ve her piksele **kameradan ne
kadar uzak olduğuna göre** farklı yoğunlukta bulanıklık uyguluyor. Gerçek bir
objektifin yaptığı şey bu.

Ek olarak:
- **Temporal smoothing** — saç ve gözlük kenarındaki kare kare titremeyi siler
- **Occlusion-aware sampling** — öndeki kişinin arkaya taşıp hale yapmasını engeller
- **Highlight bloom** — arkadaki ışıklar gri lekeye değil, bokeh diskine dönüşür

---

## ⚠️ Önce şunu oku

Bu bir **kaynak kod** projesi, hazır `.exe` değil. Kullanabilmek için önce
bilgisayarında derlemen gerekiyor. İlk defa yapıyorsan tahminen **1–2 saat**
sürer ve büyük ihtimalle bir yerde hata alırsın (bu normal).

**Sadece yayınında arkaplan bulanıklığı istiyorsan ve kod derlemekle uğraşmak
istemiyorsan:** en alttaki "Kolay yol" bölümüne atla. 5 dakikada biter.

---

## Gereken şeyler (Windows)

Hepsi ücretsiz. Sırayla kur:

### 1. Visual Studio 2022 Community
https://visualstudio.microsoft.com/downloads/

Kurulum sihirbazında **"Desktop development with C++"** kutucuğunu işaretle.
Bu önemli — işaretlemezsen derleyici gelmez. ~8 GB indirir.

### 2. CMake
https://cmake.org/download/ → "Windows x64 Installer"

Kurulumda **"Add CMake to the system PATH for all users"** seçeneğini işaretle.

### 3. Git
https://git-scm.com/download/win

Her şeyi varsayılan bırakıp "Next" geç.

### 4. ONNX Runtime (DirectML sürümü)
https://github.com/microsoft/onnxruntime/releases

En son sürümde `onnxruntime-win-x64-directml-X.XX.X.zip` dosyasını indir.
`C:\dev\onnxruntime` klasörüne çıkar. İçinde `include` ve `lib` klasörleri
görmelisin.

> DirectML sürümünü al, normal olanı değil. GPU hızlandırma bundan geliyor.

### 5. Derinlik modeli
https://huggingface.co/onnx-community/depth-anything-v2-small/tree/main/onnx

`model.onnx` dosyasını indir, adını `depth_anything_v2_small.onnx` yap ve
`C:\dev\models\` klasörüne koy. (~100 MB)

---

## Derleme

**Başlat menüsünden "x64 Native Tools Command Prompt for VS 2022"** aç.
Normal cmd değil, bu olmalı.

Şu komutları tek tek yapıştır:

```bat
cd C:\dev
git clone --recursive https://github.com/obsproject/obs-studio.git
cd obs-studio
git checkout 30.2.3
```

Bu OBS'in kaynak kodunu indirir (biraz sürer, ~2 GB).

Şimdi bu eklentinin klasörünü `C:\dev\obs-depth-bokeh` olacak şekilde koy ve:

```bat
cd C:\dev\obs-depth-bokeh
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DONNXRUNTIME_ROOT=C:/dev/onnxruntime ^
  -Dlibobs_DIR=C:/dev/obs-studio/build/libobs
cmake --build build --config Release
```

Hata almadıysan `build\Release\obs-depth-bokeh.dll` oluşmuş olmalı.

> **Hata alırsan:** en sık sebep `libobs_DIR` yolunun yanlış olması. OBS'i
> kendin derlemediysen bu klasör yoktur. O durumda daha kolay yol:
> https://github.com/obsproject/obs-plugintemplate deposunu klonla, bu
> projenin `src`, `data`, `CMakeLists.txt` dosyalarını içine kopyala ve
> template'in kendi build script'ini çalıştır — bağımlılıkları otomatik indirir.

---

## OBS'e kurma

1. **OBS'i tamamen kapat.**

2. Şu klasöre git:
   ```
   C:\Program Files\obs-studio\obs-plugins\64bit\
   ```
   Buraya kopyala:
   - `obs-depth-bokeh.dll`
   - `onnxruntime.dll`  ← `C:\dev\onnxruntime\lib\` içinden
   - `DirectML.dll`     ← aynı yerden

3. Şu klasöre git (yoksa oluştur):
   ```
   C:\Program Files\obs-studio\data\obs-plugins\obs-depth-bokeh\
   ```
   Buraya `data` klasörünün **içindekileri** kopyala. Sonuç şöyle görünmeli:
   ```
   obs-depth-bokeh\
   ├── locale\en-US.ini
   └── shaders\depth-bokeh.effect
   ```

   > Bu klasörlere yazarken Windows yönetici izni isteyecek, "Devam"a bas.

4. OBS'i aç.

---

## Kullanma

1. Webcam kaynağına **sağ tık → Filtreler**
2. Sol altta **+** → **Depth Bokeh (Background Blur)**
3. **Depth model** kutusuna `C:\dev\models\depth_anything_v2_small.onnx`
   dosyasını seç
4. Birkaç saniye bekle — ilk yüklemede model hazırlanıyor

### Ayarları tutturmak

Önce **"Show depth map (debug)"** kutusunu işaretle. Siyah-beyaz bir görüntü
göreceksin: **beyaz = yakın, siyah = uzak.** Sen beyaz, arkan siyah olmalı.
Değilse ışığın yetersiz demektir.

Sonra kutuyu kapat ve şunları ayarla:

| Ayar | Ne yapar | Başlangıç |
|---|---|---|
| **Focus distance** | Netlik hangi derinlikte | `0.85` |
| **In-focus range** | Ne kadarlık bir aralık net kalsın | `0.12` |
| **Max blur radius** | Arkaplan ne kadar bulanık | `18` |
| **Edge protection** | Kenar taşmasını engeller. Halo görüyorsan artır | `6` |
| **Temporal smoothing** | Titreme varsa artır, gecikme hissi varsa azalt | `0.35` |
| **Model resolution** | Yüksek = keskin ama ağır | `252` |
| **Depth update rate** | FPS düşüyorsa azalt | `15` |

**Sıkça karşılaşılan durumlar:**

- *Elim/mikrofonum bulanıklaşıyor* → **Focus distance**'ı biraz düşür veya
  **In-focus range**'i artır
- *Etrafımda hale var* → **Edge protection**'ı artır (8–12)
- *FPS düştü* → **Model resolution** = 126, **Depth update rate** = 10
- *Kenarlar titriyor* → **Temporal smoothing** = 0.55

---

## Kolay yol (kod derlemek istemiyorsan)

Yayınında hemen arkaplan bulanıklığı istiyorsan:

**obs-backgroundremoval** — https://github.com/locaal-ai/obs-backgroundremoval

Releases sayfasından `.exe` kurulumu indir, çift tıkla, bitti. Filtre olarak
"Background Removal" görünür, içinde blur seçeneği var. Derinlik tabanlı
kademeli bokeh yok ama işi görür.

Yukarıdaki proje, o eklentinin yapamadığı şeyi yapmak için var — ikisi rakip
değil, farklı işler.

---

## Lisans

GPLv2, OBS ile aynı.
