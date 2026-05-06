# Simulasi Peternakan Ayam Petelur
**OpenGL 3.3 Core Profile · C++17 · CMake**

Proyek Grafika Komputer — simulasi 3D alur kerja peternakan ayam petelur.

---

## Fitur yang Sudah Berjalan

| Fitur | Keterangan |
|---|---|
| **Kandang 3D** | Bangunan kandang dengan atap, dinding, dan rak kandang baris-kolom |
| **300 Ayam** | Dirender efisien menggunakan **hardware instancing** (1 draw call) |
| **Beri Pakan** | Tekan `F` → ayam "bobbing" (animasi makan) + telur muncul satu per satu |
| **Telur** | Muncul dengan animasi scale-up; tetap terlihat sampai dipanen |
| **Gerobak Roda Satu** | Tekan `H` → gerobak datang, melintas sepanjang kandang, panen semua telur, lalu kembali |
| **Pencahayaan** | Phong sederhana (ambient + diffuse) |
| **Kamera bebas** | WASD + Q/E + scroll mouse |

---

## Struktur Folder

```
simulasi-peternakan/
├── CMakeLists.txt
├── README.md
└── src/
    ├── main.cpp          ← logika utama + shader (embedded GLSL)
    ├── Mesh.h            ← kelas Mesh (VAO/VBO/EBO, instancing-ready)
    ├── Shapes.h          ← generator mesh prosedural (box, sphere, cylinder)
    ├── Model.h           ← loader Assimp (opsional, untuk model .obj)
    ├── glad.c / glad/    ← GLAD loader
    ├── stb_image.cpp/h   ← loader tekstur
    └── shaders/          ← (tidak dipakai lagi — shader embedded di main.cpp)
```

---

## Cara Build

### Windows (MinGW / Visual Studio)
```bash
cd simulasi-peternakan
mkdir build && cd build
cmake .. -G "MinGW Makefiles"   # atau -G "Visual Studio 17 2022"
cmake --build .
./SimulasiPeternakan.exe
```

### Linux / macOS
```bash
# Install dependensi (Ubuntu)
sudo apt install libglfw3-dev libgl-dev cmake g++

cd simulasi-peternakan
mkdir build && cd build
cmake ..
make -j$(nproc)
./SimulasiPeternakan
```

---

## Kontrol

| Tombol | Aksi |
|--------|------|
| `W A S D` | Gerak kamera maju/mundur/kiri/kanan |
| `Q / E` | Naik / turun kamera |
| `Scroll Mouse` | Putar arah pandang (yaw + pitch) |
| `F` | **Beri pakan** → ayam makan, telur muncul |
| `H` | **Panen** → gerobak datang, ambil telur, kembali |
| `ESC` | Keluar |

---

## Alur Simulasi

```
1. Scene tampil: kandang 3D, 300 ayam (grid kiri+kanan)
2. Tekan F  → ayam bergerak (bobbing), telur muncul satu per satu
3. Setelah ~4 detik, ayam berhenti makan
4. Tekan H  → gerobak roda satu masuk, jalan sepanjang kandang
5. Gerobak menyentuh ujung → semua telur dipanen (hilang)
6. Gerobak kembali keluar → simulasi kembali IDLE
7. Ulangi dari langkah 2
```

---

## Dependensi

- **GLFW 3.3+** — window & input
- **GLAD** — OpenGL loader (sudah disertakan di `src/glad.c`)
- **GLM** — matematika vektor/matriks (header-only, pasang via vcpkg / apt)
- **Assimp** *(opsional)* — untuk load model .obj jika ingin mengganti mesh prosedural
- **stb_image** — loader tekstur (sudah disertakan)

### Install GLM (jika belum ada)

**Ubuntu:**
```bash
sudo apt install libglm-dev
```

**Windows (vcpkg):**
```bash
vcpkg install glm glfw3
```

---

## Pengembangan Selanjutnya

- [ ] Ganti mesh prosedural dengan model `.obj` ayam & gerobak
- [ ] Tambah tekstur (kayu kandang, tanah, bulu ayam)
- [ ] Suara efek (partikel pakan jatuh, klik telur)
- [ ] Sistem waktu (siklus siang–malam)
- [ ] UI overlay (jumlah telur, skor)
