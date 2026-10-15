//pastikan cpuesp8266 diset di 160Mhz
//pilih flash size 4MB(FS:3MB OTA:~512KB)
//set #define SPI_FREQUENCY  80000000 untuk layar pada library tft
//untuk wiring layar #define TFT_MOSI  PIN_D7  //gpio 13
      // TFT_SCLK  gpio 14
      // TFT_CS    gpio 15
      // TFT_DC    gpio 0
      // TFT_RST   gpio 2
      // TFT_SDA   gpio 13
      // TFT_SCL   gpio 14
//  ina226 gpio 4 untuk SDA, gpio 5 untuk SCL, gpio 9 ke ALE
// buat ke mosfet gpio 10
// buat ke tombol up gpio 1
// buat ke tombol ok gpio 16
// buat ke tombol down gpio 3

// nang meulah ini orang biasa nang kadatapi bisa ngeprogram jadi maaf kalo banyak salah atau kekurangan

//buat setelah diupload misal layar tft jadi tampil hitam coba klik ok sekali, misal bisa masuk ke tampilan power meter berarti aman
//soalnya tampilan utama itu tampilan Gif, jadi perlu upload gif dulu dengan cara aktifkan wifi lalu buka web //192.168.4.1
//pada tampilan web paling bawah ada untuk pilih gif dan upload jika udah ada file gif yang dipilih

#include <SPI.h>
#include <TFT_eSPI.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>
#include <AnimatedGIF.h>
#include <Wire.h>
#include <INA226.h> // Library Rob Tillaart

#define BUTTON_OK 16      // GPIO 16: pilih menu / ON-OFF beban (hanya tekan pendek)
#define BUTTON_UP 1       // GPIO 1 (TX): menu naik / nilai naik | tahan 3 detik = masuk/keluar mode pilih menu
#define BUTTON_DOWN 3     // GPIO 3 (RX): menu turun / nilai turun
#define MOSFET_PIN 10     // GPIO 10 untuk Kontrol MOSFET (Cut-Off & PWM)
#define ALERT_PIN 9       // GPIO 9 terhubung ke Pin ALE INA226
#define I2C_ADDRESS 0x40

// Pengaturan skala GIF
#define GIF_FIT 1
#define MAX_LINE 240      // lebar maksimum layar yang didukung buffer baris

// Grafik osiloskop
#define GRAF_W 104        // jumlah sampel = lebar area grafik (piksel)

const char* ssid = "gif esp8266";
const char* password = "12345678";

ESP8266WebServer server(80);
TFT_eSPI tft = TFT_eSPI();
AnimatedGIF gif;
INA226 INA(I2C_ADDRESS);

fs::File gifFile;     
fs::File uploadFile;   
String currentGifPath = "/animated.gif";
volatile bool uploadSelesai = false;
volatile bool gifBerubah = false;  
bool adaGif = false;
bool ina226Aktif = false;
float currentLimit_A = 0.0;             // 0.0 = Fitur Limit Dinonaktifkan
uint8_t limitMode = 1;                  // Mode 1: Cut-Off (ON/OFF), Mode 2: Constant Current PWM
int pwmDuty = 0;                        // Duty cycle PWM (0 - 1023)

volatile bool mosfetState = false;    
volatile bool tripOccurred = false;    
bool autoResetTrip = false;            
unsigned long lastTripTime = 0;       

uint8_t modeTampilan = 0;
bool wifiAktif = false;

unsigned long pressStartUP = 0;
bool isPressingUP = false;
bool longPressUPExecuted = false;

unsigned long lastButtonScan = 0;
const unsigned long LONG_PRESS_TIME = 3000;

//  MENU_TUTUP : OK pendek = ON/OFF beban
//  MENU_PILIH : UP/DOWN pindah menu, OK pendek = pilih/edit
//  MENU_EDIT  : UP/DOWN ubah nilai, OK pendek = kembali ke pilih menu
enum MenuState {
  MENU_TUTUP,
  MENU_PILIH,
  MENU_EDIT
};
MenuState menuState = MENU_TUTUP;
uint8_t menuIndex = 0;
// Tampilan pengukur : 0:CC 1:MODE 2:WIFI 3:PENGATUR 4:KE GIF
// Tampilan grafik   : sama + 5:KEMBALI
const uint8_t MENU_JUMLAH = 5;
const uint8_t MENU_JUMLAH_GRAFIK = 6;

// Layar pengaturan
//  0: skala set CC | 1: peralihan otomatis ke GIF | 2: waktu peralihan | 3: tampilan grafik | 4: kembali
enum SettingState {
  SET_PILIH,
  SET_EDIT
};
SettingState settingState = SET_PILIH;
uint8_t settingIndex = 0;
const uint8_t SETTING_JUMLAH = 5;

// Skala set CC
uint8_t ccStepIndex = 1;                          // default 0.100A
const float CC_STEP[3] = {1.0f, 0.1f, 0.01f};
const char* const CC_STEP_LABEL[3] = {"1.00A", "0.100A", "0.010A"};

// Peralihan otomatis ke GIF bila tidak ada arus
bool autoKembaliAktif = false;
uint8_t autoKembaliMenit = 5;
unsigned long mulaiTanpaArus = 0;

// Data grafik
float histV[GRAF_W];
float histA[GRAF_W];
float grafV = 0.0;
float grafA = 0.0;

uint16_t usLineBuffer[MAX_LINE];

int gifOX = 0, gifOY = 0, gifOW = 160, gifOH = 128;
uint32_t gifXStep = 65536, gifYStep = 65536;

float peakPower_W = 0.0;
float energy_Wh = 0.0;
float capacity_Ah = 0.0;
unsigned long lastEnergyCalc = 0;

void perbaruiTampilanINA(bool forceRedraw = false);
void perbaruiTampilanGrafik(bool forceRedraw = false);
bool cekAutoKembaliKeUtama(float current_A);
void keTampilanGIF();
void keTampilanPengukur();
void keTampilanPengaturan();
void keTampilanGrafik();
void toggleWifi();
void gambarLayoutINA();
void gambarLayoutGrafik();
void gambarPengaturan();
void segarkanTampilan();
void gambarUlangTampilan();

void gambarIkonWifi(int16_t x, int16_t y) {
  if (!wifiAktif) return;
  uint16_t color = TFT_CYAN;
  tft.fillCircle(x + 7, y + 11, 1, color);
  tft.drawCircleHelper(x + 7, y + 11, 4, 2, color);
  tft.drawCircleHelper(x + 7, y + 11, 7, 2, color);
  tft.drawCircleHelper(x + 7, y + 11, 10, 2, color);
}

void hitungSkalaGIF(int gw, int gh) {
  int sw = tft.width();
  int sh = tft.height();
  if (sw > MAX_LINE) sw = MAX_LINE;
  if (gw < 1) gw = 1;
  if (gh < 1) gh = 1;

#if GIF_FIT
  if ((long)sw * gh <= (long)sh * gw) {
    gifOW = sw;
    gifOH = (int)((long)gh * sw / gw);
  } else {
    gifOH = sh;
    gifOW = (int)((long)gw * sh / gh);
  }
#else
  gifOW = sw;
  gifOH = sh;
#endif

  if (gifOW < 1) gifOW = 1;
  if (gifOH < 1) gifOH = 1;
  if (gifOW > sw) gifOW = sw;
  if (gifOH > sh) gifOH = sh;

  gifOX = (tft.width() - gifOW) / 2;
  gifOY = (tft.height() - gifOH) / 2;
  gifXStep = ((uint32_t)gw << 16) / (uint32_t)gifOW;
  gifYStep = ((uint32_t)gh << 16) / (uint32_t)gifOH;
  if (gifXStep == 0) gifXStep = 1;
  if (gifYStep == 0) gifYStep = 1;
}

void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s = pDraw->pPixels;
  uint16_t *pPalette = pDraw->pPalette;
  uint32_t sy = (uint32_t)(pDraw->iY + pDraw->y);
  uint32_t iX = (uint32_t)pDraw->iX;
  int iW = pDraw->iWidth;

  int dy0 = (int)((((uint32_t)sy << 16) + gifYStep - 1) / gifYStep);
  int dy1 = (int)(((((uint32_t)(sy + 1)) << 16) + gifYStep - 1) / gifYStep) - 1;
  if (dy1 >= gifOH) dy1 = gifOH - 1;
  if (dy0 > dy1) return;  

  int dx0 = (int)(((iX << 16) + gifXStep - 1) / gifXStep);
  int dx1 = (int)((((iX + (uint32_t)iW) << 16) + gifXStep - 1) / gifXStep) - 1;
  if (dx1 >= gifOW) dx1 = gifOW - 1;
  if (dx0 > dx1) return;

  bool trans = pDraw->ucHasTransparency;
  uint8_t tc = pDraw->ucTransparent;
  int runStart = -1;

  for (int dx = dx0; dx <= dx1 + 1; dx++) {
    bool valid = false;
    if (dx <= dx1) {
      int idx = (int)(((uint32_t)dx * gifXStep) >> 16) - (int)iX;
      if (idx < 0) idx = 0;
      if (idx >= iW) idx = iW - 1;
      uint8_t c = s[idx];
      if (!(trans && c == tc)) {
        usLineBuffer[dx] = pPalette[c];
        valid = true;
      }
    }

    if (valid) {
      if (runStart < 0) runStart = dx;
    } else if (runStart >= 0) {
      int len = dx - runStart;
      for (int dy = dy0; dy <= dy1; dy++) {
        tft.pushImage(gifOX + runStart, gifOY + dy, len, 1, &usLineBuffer[runStart]);
      }
      runStart = -1;
    }
  }
}

void * GIFOpenFile(const char *fname, int32_t *pSize) {
  gifFile = LittleFS.open(fname, "r");
  if (gifFile) {
    *pSize = gifFile.size();
    return (void *)&gifFile;
  }
  return NULL;
}

void GIFCloseFile(void *pHandle) {
  fs::File *f = (fs::File *)pHandle;
  if (f && *f) f->close();
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  int32_t iBytesRead = pFile->iSize - pFile->iPos;
  if (iBytesRead > iLen) iBytesRead = iLen;
  if (iBytesRead <= 0) return 0;

  fs::File *f = (fs::File *)pFile->fHandle;
  iBytesRead = f->read(pBuf, iBytesRead);
  pFile->iPos += iBytesRead;
  return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  fs::File *f = (fs::File *)pFile->fHandle;
  f->seek(iPosition, SeekSet);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Power Meter & Control Panel</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: 'Segoe UI', Arial, sans-serif; background: #0a0a0a; color: white; margin: 0; padding: 15px; }
    .container { max-width: 900px; margin: 0 auto; display: flex; flex-direction: column; gap: 15px; }
    .nav { display: flex; gap: 10px; border-bottom: 2px solid #222; padding-bottom: 10px; }
    .nav-btn { background: #181818; color: #888; border: 1px solid #333; padding: 8px 16px; border-radius: 6px; cursor: pointer; font-weight: bold; }
    .nav-btn.active { background: #00e5ff; color: #000; border-color: #00e5ff; }
    .tab-content { display: none; flex-direction: column; gap: 15px; }
    .tab-content.active { display: flex; }
    .panel { background: #121212; border: 1px solid #222; border-radius: 8px; padding: 15px; }
    
    .chart-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
    .chart-title { margin: 0; font-size: 16px; font-weight: bold; }
    .chart-value { font-size: 20px; font-weight: bold; font-family: monospace; }
    
    .gauge-container { display: flex; justify-content: space-around; align-items: center; flex-wrap: wrap; gap: 20px; padding: 15px 0; }
    .gauge-card { text-align: center; position: relative; width: 220px; margin: 0; }
    .gauge-svg { width: 100%; height: 120px; overflow: visible; }
    .gauge-bg { fill: none; stroke: #222; stroke-width: 6; stroke-linecap: round; stroke-dasharray: 157; stroke-dashoffset: 0; }
    .gauge-fill { fill: none; stroke-width: 6; stroke-linecap: round; stroke-dasharray: 157; stroke-dashoffset: 157; transition: stroke-dashoffset 0.3s ease; }
    .gauge-val { font-size: 18px; font-weight: bold; margin-top: -5px; }
    
    .needle-v { stroke: #00e5ff; stroke-width: 2; stroke-linecap: round; transition: transform 0.3s ease; transform-origin: 50px 50px; }
    .needle-a { stroke: #00ff66; stroke-width: 2; stroke-linecap: round; transition: transform 0.3s ease; transform-origin: 50px 50px; }
    .needle-w { stroke: #d500f9; stroke-width: 2; stroke-linecap: round; transition: transform 0.3s ease; transform-origin: 50px 50px; }
    .pin { fill: #ffffff; stroke: #121212; stroke-width: 1.5; r: 3.5; }

    .v-color { stroke: #00e5ff; color: #00e5ff; }
    .a-color { stroke: #00ff66; color: #00ff66; }
    .w-color { stroke: #d500f9; color: #d500f9; }

    .widgets-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; }
    .widget { border-radius: 8px; padding: 12px; background: #121212; position: relative; min-height: 90px; }
    .widget-title { font-size: 11px; font-weight: bold; color: #aaa; text-transform: uppercase; }
    .widget-unit { position: absolute; right: 12px; top: 12px; font-size: 12px; color: #888; }
    .widget-value { font-size: 22px; font-weight: bold; margin-top: 15px; font-family: monospace; }
    
    .w-v { border: 1px solid #00e5ff; } .w-v .widget-value { color: #00e5ff; }
    .w-a { border: 1px solid #00ff66; } .w-a .widget-value { color: #00ff66; }
    .w-w { border: 1px solid #d500f9; } .w-w .widget-value { color: #d500f9; }
    .w-cc { border: 1px solid #ff1744; } .w-cc .widget-value { color: #ff1744; }
    .w-wp { border: 1px solid #ffab00; } .w-wp .widget-value { color: #ffab00; }
    .w-wh { border: 1px solid #ff3d00; } .w-wh .widget-value { color: #ff3d00; }
    .w-ah { border: 1px solid #00e676; } .w-ah .widget-value { color: #00e676; }
    .w-r { border: 1px solid #ff4081; } .w-r .widget-value { color: #ff4081; }
    
    .w-ctrl { border: 1px solid #7c4dff; display: flex; flex-direction: column; gap: 8px; justify-content: center; }
    .ctrl-input, .ctrl-select { background: #1a1a1a; border: 1px solid #333; color: white; padding: 5px; border-radius: 4px; text-align: center; width: 100%; }
    .ctrl-btn { background: #7c4dff; color: white; border: none; padding: 6px; border-radius: 4px; font-weight: bold; cursor: pointer; }
    
    .auto-reset-box { display: flex; align-items: center; justify-content: space-between; background: #181818; padding: 5px 8px; border-radius: 4px; border: 1px solid #2a2a2a; }
    .auto-reset-label { font-size: 10px; font-weight: bold; color: #ffab00; }
    .toggle-btn { background: #333; color: #aaa; border: none; padding: 4px 8px; font-size: 10px; font-weight: bold; border-radius: 3px; cursor: pointer; }
    .toggle-btn.active { background: #ffab00; color: #000; }

    .w-upload { border: 1px solid #00e5ff; display: flex; flex-direction: column; justify-content: center; align-items: center; }
    input[type=file] { display: none; }
    .upload-btn { background: #00e5ff; color: #000; padding: 6px 12px; font-size: 12px; font-weight: bold; border-radius: 4px; cursor: pointer; text-align: center; }
    .submit-btn { background: transparent; border: 1px solid #00e5ff; color: #00e5ff; margin-top: 5px; padding: 4px 8px; font-size: 11px; border-radius: 4px; cursor: pointer; }
    
    canvas { background: #121212; border: 1px solid #222; border-radius: 8px; width: 100%; height: 260px; }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
      <button class="nav-btn active" onclick="switchTab('dashboard')">DASHBOARD</button>
      <button class="nav-btn" onclick="switchTab('grafik')">GRAFIK REALTIME</button>
    </div>

    <div id="tab-dashboard" class="tab-content active">
      <div class="panel gauge-container">
        <div class="gauge-card">
          <svg class="gauge-svg" viewBox="0 0 100 60">
            <path class="gauge-bg" d="M 10 50 A 40 40 0 0 1 90 50" />
            <path id="gauge-v" class="gauge-fill v-color" d="M 10 50 A 40 40 0 0 1 90 50" />
            <line id="needle-v" class="needle-v" x1="50" y1="50" x2="15" y2="50" />
            <circle class="pin" cx="50" cy="50" />
          </svg>
          <div id="val-v-g" class="gauge-val v-color">0.000 V</div>
        </div>

        <div class="gauge-card">
          <svg class="gauge-svg" viewBox="0 0 100 60">
            <path class="gauge-bg" d="M 10 50 A 40 40 0 0 1 90 50" />
            <path id="gauge-a" class="gauge-fill a-color" d="M 10 50 A 40 40 0 0 1 90 50" />
            <line id="needle-a" class="needle-a" x1="50" y1="50" x2="15" y2="50" />
            <circle class="pin" cx="50" cy="50" />
          </svg>
          <div id="val-a-g" class="gauge-val a-color">0.000 A</div>
        </div>

        <div class="gauge-card">
          <svg class="gauge-svg" viewBox="0 0 100 60">
            <path class="gauge-bg" d="M 10 50 A 40 40 0 0 1 90 50" />
            <path id="gauge-w" class="gauge-fill w-color" d="M 10 50 A 40 40 0 0 1 90 50" />
            <line id="needle-w" class="needle-w" x1="50" y1="50" x2="15" y2="50" />
            <circle class="pin" cx="50" cy="50" />
          </svg>
          <div id="val-w-g" class="gauge-val w-color">0.000 W</div>
        </div>
      </div>

      <div class="widgets-grid">
        <div class="widget w-v"><span class="widget-title">TEGANGAN</span><span class="widget-unit">V</span><div id="val-v" class="widget-value">00.000</div></div>
        <div class="widget w-a"><span class="widget-title">ARUS</span><span class="widget-unit">A</span><div id="val-a" class="widget-value">00.000</div></div>
        <div class="widget w-w"><span class="widget-title">DAYA</span><span class="widget-unit">W</span><div id="val-w" class="widget-value">00.000</div></div>
        <div class="widget w-cc"><span class="widget-title">LIMIT CURRENT (CC)</span><span class="widget-unit">A</span><div id="val-cc" class="widget-value">00.000</div></div>
        <div class="widget w-wp"><span class="widget-title">PEAK POWER</span><span class="widget-unit">Wp</span><div id="val-wp" class="widget-value">00.000</div></div>
        <div class="widget w-wh"><span class="widget-title">ENERGI</span><span class="widget-unit">Wh</span><div id="val-wh" class="widget-value">00.000</div></div>
        <div class="widget w-ah"><span class="widget-title">KAPASITAS</span><span class="widget-unit">Ah</span><div id="val-ah" class="widget-value">00.000</div></div>
        <div class="widget w-r"><span class="widget-title">RESISTANSI</span><span class="widget-unit">&Omega;</span><div id="val-r" class="widget-value">00.000</div></div>
        
        <div class="widget w-ctrl">
          <span class="widget-title" style="color: #7c4dff;">SET LIMIT CURRENT</span>
          <input type="number" step="0.01" id="input-limit" class="ctrl-input" placeholder="Misal: 2.5">
          <select id="select-mode" class="ctrl-select">
            <option value="1">Mode 1: Cut-Off (ON/OFF)</option>
            <option value="2">Mode 2: Constant Current (PWM)</option>
          </select>
          <button class="ctrl-btn" onclick="setLimit()">SIMPAN LIMIT</button>
          
          <div class="auto-reset-box">
            <span class="auto-reset-label">AUTO RESET TRIP</span>
            <button id="btn-autoreset" class="toggle-btn" onclick="toggleAutoReset()">OFF</button>
          </div>

          <button id="btn-mosfet" class="ctrl-btn" style="background:#ff9100; color:#000;" onclick="toggleMosfet()">MOSFET: OFF</button>
        </div>

        <div class="widget w-upload">
          <form method="POST" action="/upload" enctype="multipart/form-data" style="text-align:center;">
            <label for="file-input" class="upload-btn">PILIH GIF</label>
            <input id="file-input" type="file" name="upload" accept=".gif" onchange="showName(this)" required>
            <div id="file-name" style="font-size: 10px; color: #888; margin-top: 3px;"></div>
            <input type="submit" value="UPLOAD" class="submit-btn">
          </form>
        </div>
      </div>
    </div>

    <div id="tab-grafik" class="tab-content">
      <div class="panel">
        <div class="chart-header">
          <h4 class="chart-title" style="color: #00e5ff;">GRAFIK TEGANGAN (V)</h4>
          <div id="chart-val-v" class="chart-value" style="color: #00e5ff;">0.000 V</div>
        </div>
        <canvas id="chart-v"></canvas>
      </div>

      <div class="panel">
        <div class="chart-header">
          <h4 class="chart-title" style="color: #00ff66;">GRAFIK ARUS (A)</h4>
          <div id="chart-val-a" class="chart-value" style="color: #00ff66;">0.000 A</div>
        </div>
        <canvas id="chart-a"></canvas>
      </div>
    </div>
  </div>

  <script>
    function switchTab(tabName) {
      document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));
      if(tabName === 'dashboard') {
        document.getElementById('tab-dashboard').classList.add('active');
        event.target.classList.add('active');
      } else {
        document.getElementById('tab-grafik').classList.add('active');
        event.target.classList.add('active');
      }
    }

    function setGauge(gaugeId, needleId, val, maxVal) {
      let perc = Math.min(Math.max(val / maxVal, 0), 1);
      let offset = 157 - (157 * perc);
      document.getElementById(gaugeId).style.strokeDashoffset = offset;
      let angle = perc * 180;
      document.getElementById(needleId).style.transform = 'rotate(' + angle + 'deg)';
    }

    function showName(input) {
      if(input.files && input.files[0]) {
        document.getElementById('file-name').innerText = input.files[0].name;
      }
    }

    function setLimit() {
      let val = document.getElementById('input-limit').value;
      let mode = document.getElementById('select-mode').value;
      fetch('/setLimit?val=' + val + '&mode=' + mode)
        .then(response => response.text())
        .then(res => alert('Limit Current Berhasil Disimpan: ' + val + ' A (Mode ' + mode + ')'));
    }

    function toggleAutoReset() {
      fetch('/toggleAutoReset')
        .then(response => response.json())
        .then(data => {
          updateAutoResetBtn(data.autoReset);
        });
    }

    function updateAutoResetBtn(state) {
      let btn = document.getElementById('btn-autoreset');
      if(state) {
        btn.innerText = "ON";
        btn.classList.add('active');
      } else {
        btn.innerText = "OFF";
        btn.classList.remove('active');
      }
    }

    function toggleMosfet() {
      fetch('/toggleMosfet')
        .then(response => response.json())
        .then(data => {
          updateMosfetButton(data.state, data.trip);
        });
    }

    function updateMosfetButton(state, trip) {
      let btn = document.getElementById('btn-mosfet');
      if(trip) {
        btn.innerText = "TRIPPED! (KLIK RESET)";
        btn.style.background = "#ff1744";
        btn.style.color = "#ffffff";
      } else if(state) {
        btn.innerText = "MOSFET: ON";
        btn.style.background = "#00e676";
        btn.style.color = "#000000";
      } else {
        btn.innerText = "MOSFET: OFF";
        btn.style.background = "#ff9100";
        btn.style.color = "#000000";
      }
    }

    class RollingChart {
      constructor(canvasId, strokeColor, maxVal) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
        this.color = strokeColor;
        this.maxVal = maxVal;
        this.data = new Array(100).fill(0);
        this.resize();
      }

      resize() {
        this.canvas.width = this.canvas.parentElement.clientWidth - 30;
        this.canvas.height = 200;
      }

      addData(val) {
        this.data.push(val);
        this.data.shift();
        this.draw();
      }

      draw() {
        let w = this.canvas.width;
        let h = this.canvas.height;
        this.ctx.clearRect(0, 0, w, h);

        this.ctx.strokeStyle = '#222';
        this.ctx.lineWidth = 1;
        for(let i=1; i<4; i++) {
          this.ctx.beginPath();
          this.ctx.moveTo(0, h * (i/4));
          this.ctx.lineTo(w, h * (i/4));
          this.ctx.stroke();
        }

        this.ctx.strokeStyle = this.color;
        this.ctx.lineWidth = 2;
        this.ctx.beginPath();

        let step = w / (this.data.length - 1);
        for(let i = 0; i < this.data.length; i++) {
          let x = i * step;
          let y = h - (Math.min(this.data[i] / this.maxVal, 1) * h);
          if(i === 0) this.ctx.moveTo(x, y);
          else this.ctx.lineTo(x, y);
        }
        this.ctx.stroke();
      }
    }

    let chartV = new RollingChart('chart-v', '#00e5ff', 30);
    let chartA = new RollingChart('chart-a', '#00ff66', 10);

    window.addEventListener('resize', () => {
      chartV.resize();
      chartA.resize();
    });

    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('val-v').innerText = data.v.toFixed(3);
          document.getElementById('val-a').innerText = data.a.toFixed(3);
          document.getElementById('val-w').innerText = data.w.toFixed(3);
          document.getElementById('val-cc').innerText = data.cc.toFixed(3);
          document.getElementById('val-wp').innerText = data.wp.toFixed(3);
          document.getElementById('val-wh').innerText = data.wh.toFixed(3);
          document.getElementById('val-ah').innerText = data.ah.toFixed(3);
          document.getElementById('val-r').innerText = data.r > 999 ? '---' : data.r.toFixed(3);

          document.getElementById('val-v-g').innerText = data.v.toFixed(3) + " V";
          document.getElementById('val-a-g').innerText = data.a.toFixed(3) + " A";
          document.getElementById('val-w-g').innerText = data.w.toFixed(3) + " W";

          setGauge('gauge-v', 'needle-v', data.v, 30);  
          setGauge('gauge-a', 'needle-a', data.a, 80);
          setGauge('gauge-w', 'needle-w', data.w, 2400); 

          document.getElementById('chart-val-v').innerText = data.v.toFixed(3) + " V";
          document.getElementById('chart-val-a').innerText = data.a.toFixed(3) + " A";

          updateMosfetButton(data.mosfet, data.trip);
          updateAutoResetBtn(data.autoReset);

          chartV.addData(data.v);
          chartA.addData(data.a);
        });
    }

    setInterval(updateData, 500);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", DASHBOARD_HTML);
}

void handleSetLimit() {
  if (server.hasArg("val")) {
    currentLimit_A = server.arg("val").toFloat();
    tripOccurred = false; 
  }
  if (server.hasArg("mode")) {
    limitMode = server.arg("mode").toInt();
    if (limitMode == 2 && mosfetState) {
      pwmDuty = 0; 
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleToggleAutoReset() {
  autoResetTrip = !autoResetTrip;
  String json = "{\"autoReset\":" + String(autoResetTrip ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleToggleMosfet() {
  tripOccurred = false;
  mosfetState = !mosfetState;
  
  if (limitMode == 2) {
    pwmDuty = 0; // Soft-start naik dari 0
  } else {
    pwmDuty = mosfetState ? 1023 : 0;
  }
  
  digitalWrite(MOSFET_PIN, mosfetState ? HIGH : LOW);
  
  String json = "{\"state\":" + String(mosfetState ? "true" : "false") + 
                ",\"trip\":" + String(tripOccurred ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void hitungParameterListrik(float busvoltage, float current_A, float power_W) {
  unsigned long now = millis();
  float deltaTimeSeconds = (now - lastEnergyCalc) / 1000.0;
  if (lastEnergyCalc == 0 || deltaTimeSeconds > 2.0) deltaTimeSeconds = 0; 
  lastEnergyCalc = now;

  if (mosfetState) {
    if (limitMode == 1) {
      // Mode 1: Cut-Off (ON/OFF)
      if (currentLimit_A > 0.0 && current_A >= currentLimit_A) {
        digitalWrite(MOSFET_PIN, LOW);
        mosfetState = false;
        tripOccurred = true;
        lastTripTime = now;
      }
    } else if (limitMode == 2) {
      // Mode 2: Soft-Start & Constant Current (PWM)
      if (currentLimit_A > 0.0) {
        if (current_A > currentLimit_A) {
          if (pwmDuty > 15) pwmDuty -= 15;
          else pwmDuty = 0;
        } else if (current_A < currentLimit_A && pwmDuty < 1023) {
          pwmDuty += 10;
          if (pwmDuty > 1023) pwmDuty = 1023;
        }
      } else {
        if (pwmDuty < 1023) {
          pwmDuty += 15;
          if (pwmDuty > 1023) pwmDuty = 1023;
        }
      }
      analogWrite(MOSFET_PIN, pwmDuty);
    }
  } else if (limitMode == 1) {
    digitalWrite(MOSFET_PIN, LOW);
  }

  if (autoResetTrip && tripOccurred && !mosfetState && limitMode == 1) {
    if (now - lastTripTime >= 3000) {
      tripOccurred = false;
      mosfetState = true;
      pwmDuty = 1023;
      digitalWrite(MOSFET_PIN, HIGH);
    }
  }

  if (power_W > peakPower_W) {
    peakPower_W = power_W;
  }

  energy_Wh += power_W * (deltaTimeSeconds / 3600.0);
  capacity_Ah += current_A * (deltaTimeSeconds / 3600.0);
}

// Proteksi arus tetap jalan walau tampilan sedang di mode GIF / pengaturan
void proteksiLatar() {
  static unsigned long lastRun = 0;
  if (!ina226Aktif || !mosfetState) return;
  unsigned long now = millis();
  if (now - lastRun < 100) return;
  lastRun = now;
  hitungParameterListrik(INA.getBusVoltage(), INA.getCurrent(), INA.getPower());
}

void handleData() {
  float busvoltage = 0.0;
  float current_A = 0.0;
  float power_W = 0.0;
  float resistance_R = 0.0;

  if (ina226Aktif) {
    busvoltage = INA.getBusVoltage();
    current_A  = INA.getCurrent(); 
    power_W    = INA.getPower();
    
    if (current_A > 0.001) {
      resistance_R = busvoltage / current_A;
    } else {
      resistance_R = 9999.0;
    }

    hitungParameterListrik(busvoltage, current_A, power_W);
  }
  String json = "{\"v\":" + String(busvoltage, 3) + 
                ",\"a\":" + String(current_A, 3) + 
                ",\"w\":" + String(power_W, 3) + 
                ",\"cc\":" + String(currentLimit_A, 2) + 
                ",\"wp\":" + String(peakPower_W, 3) + 
                ",\"wh\":" + String(energy_Wh, 3) + 
                ",\"ah\":" + String(capacity_Ah, 3) + 
                ",\"r\":" + String(resistance_R, 3) + 
                ",\"mosfet\":" + String(mosfetState ? "true" : "false") + 
                ",\"trip\":" + String(tripOccurred ? "true" : "false") + 
                ",\"autoReset\":" + String(autoResetTrip ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleUploadResponse() {
  server.send(200, "text/html", 
    "<html><body style='background:#0a0a0a;color:white;font-family:sans-serif;text-align:center;padding-top:50px;'>"
    "<h3>Berhasil Diunggah!</h3>"
    "<p>GIF baru langsung dipakai. Kembali ke dashboard...</p>"
    "<script>setTimeout(function(){ window.location.href='/'; }, 1500);</script>"
    "</body></html>"
  );
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    adaGif = false;
    uploadSelesai = false;
    gifBerubah = true;
    gif.close(); 

    if (LittleFS.exists(currentGifPath)) {
      LittleFS.remove(currentGifPath);
    }
    uploadFile = LittleFS.open(currentGifPath, "w");
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
      yield(); 
    }
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      adaGif = true;
      uploadSelesai = true;
      Serial.println("UPLOAD GIF SELESAI");
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    LittleFS.remove(currentGifPath);
    adaGif = false;
  }
}

bool inisialisasiINA226() {
  if (INA.begin()) {
    INA.setMaxCurrentShunt(80.0, 0.001); // Limit 80A, Resistor Shunt 0.001 Ohm
    return true;
  }
  return false;
}

void gambarIkonPeringatan(int16_t x, int16_t y) {
  tft.fillTriangle(x + 6, y, x, y + 11, x + 12, y + 11, TFT_YELLOW);
  tft.drawFastVLine(x + 6, y + 3, 5, TFT_BLACK);
  tft.drawPixel(x + 6, y + 9, TFT_BLACK);
}

void gambarLayoutINA() {
  tft.fillScreen(TFT_BLACK);
  perbaruiTampilanINA(true); 
}

uint8_t jumlahMenu() {
  return (modeTampilan == 3) ? MENU_JUMLAH_GRAFIK : MENU_JUMLAH;
}

// Daftar menu
void gambarDaftarMenu() {
  bool grafik = (modeTampilan == 3);
  uint8_t jumlah = jumlahMenu();
  char buf[14];
  char line[14];
  const int x = 108;
  const int y0 = grafik ? 30 : 38;
  const int yHint = grafik ? 104 : 100;

  tft.setTextDatum(TL_DATUM);

  for (uint8_t i = 0; i < jumlah; i++) {
    switch (i) {
      case 0: snprintf(buf, sizeof(buf), "CC%.2fA", currentLimit_A); break;
      case 1: snprintf(buf, sizeof(buf), "MODE %d", limitMode); break;
      case 2: snprintf(buf, sizeof(buf), "WIFI %s", wifiAktif ? "ON" : "OFF"); break;
      case 3: snprintf(buf, sizeof(buf), "PENGATUR"); break;
      case 4: snprintf(buf, sizeof(buf), "KE GIF"); break;
      default: snprintf(buf, sizeof(buf), "KEMBALI"); break;
    }

    uint16_t fg = TFT_LIGHTGREY;
    uint16_t bg = TFT_BLACK;
    if (menuState != MENU_TUTUP && i == menuIndex) {
      if (menuState == MENU_EDIT) { fg = TFT_BLACK; bg = TFT_GREEN; }
      else { fg = TFT_WHITE; bg = TFT_DARKGREY; }
    }

    snprintf(line, sizeof(line), "%-8s", buf);
    tft.setTextColor(fg, bg);
    tft.drawString(line, x, y0 + i * 12, 1);
  }

  // Petunjuk fungsi tombol OK
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  if (menuState == MENU_TUTUP) tft.drawString("OK:BEBAN", x, yHint, 1);
  else if (menuState == MENU_PILIH) tft.drawString("OK:PILIH", x, yHint, 1);
  else tft.drawString("OK:BACK ", x, yHint, 1);
}

void gambarPanelKanan() {
  char bufCC[12];
  snprintf(bufCC, sizeof(bufCC), "CC:%.2f", currentLimit_A);

  tft.setTextDatum(TL_DATUM);
  if (tripOccurred) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("CC   ", 110, 15, 2);
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(bufCC, 110, 15, 2);
  }

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  if (limitMode == 1) {
    tft.drawString("mod 1", 110, 0, 2);
  } else {
    tft.drawString("mod 2", 110, 0, 2);
  }

  // Status MOSFET
  if (mosfetState) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("ON ", 137, 115, 2);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("OFF", 137, 115, 2);
  }

  if (!ina226Aktif) {
    gambarIkonPeringatan(138, 110);
  }

  if (wifiAktif) {
    gambarIkonWifi(142, 2);
  }

  gambarDaftarMenu();
}

void perbaruiTampilanINA(bool forceRedraw) {
  static unsigned long lastUpdate = 0;
  if (!forceRedraw && (millis() - lastUpdate < 200)) return; 
  lastUpdate = millis();

  float busvoltage = 0.0;
  float current_A = 0.0;
  float power_W = 0.0;
  float resistance_R = 0.0;

  if (ina226Aktif) {
    busvoltage = INA.getBusVoltage();
    current_A  = INA.getCurrent(); 
    power_W    = INA.getPower();   
    
    if (current_A > 0.001) {
      resistance_R = busvoltage / current_A;
    } else {
      resistance_R = 9999.0;
    }

    hitungParameterListrik(busvoltage, current_A, power_W);
  }
  if (cekAutoKembaliKeUtama(current_A)) return; 

  char bufV[10], bufA[10], bufW[10], bufWp[10], bufR[10];
  snprintf(bufV, sizeof(bufV), "%06.3f", busvoltage);
  snprintf(bufA, sizeof(bufA), "%06.3f", current_A);
  snprintf(bufW, sizeof(bufW), "%06.3f", power_W);
  snprintf(bufWp, sizeof(bufWp), "%06.3f", peakPower_W);
  
  if (resistance_R > 999.0) {
    snprintf(bufR, sizeof(bufR), "000.00");
  } else {
    snprintf(bufR, sizeof(bufR), "%06.2f", resistance_R);
  }

  tft.setTextDatum(TL_DATUM); 
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(bufV, 1, 0, 4);
  tft.drawString("V", 90, 0, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(bufA, 1, 24, 4);
  tft.drawString("A", 90, 24, 2);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.drawString(bufW, 1, 48, 4);
  tft.drawString("W", 90, 48, 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(bufWp, 1, 72, 4);
  tft.drawString("Wp", 90, 72, 2);
  tft.setTextColor(TFT_PINK, TFT_BLACK);
  tft.drawString(bufR, 1, 96, 4);
  tft.drawString("R", 90, 96, 2);

  gambarPanelKanan();
}

void gambarGrafik(int16_t y, int16_t h, float *data, float spanMin, uint16_t color, const char *unit) {
  const int16_t x = 1;
  const int16_t w = GRAF_W + 2;

  float vmax = spanMin;
  for (int i = 0; i < GRAF_W; i++) {
    if (data[i] > vmax) vmax = data[i];
  }

  tft.fillRect(x, y, w, h, TFT_BLACK);
  tft.drawRect(x, y, w, h, TFT_DARKGREY);

  for (int16_t gx = x + 2; gx < x + w - 2; gx += 6) {
    tft.drawPixel(gx, y + h / 2, TFT_DARKGREY);
  }

  char lbl[12];
  snprintf(lbl, sizeof(lbl), "%.1f%s", vmax, unit);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString(lbl, x + 3, y + 2, 1);

  int16_t ih = h - 3;        
  int16_t prevY = 0;
  for (int i = 0; i < GRAF_W; i++) {
    float v = data[i];
    if (v < 0.0) v = 0.0;
    int16_t py = (y + h - 2) - (int16_t)((v / vmax) * (float)ih);
    if (py < y + 1) py = y + 1;
    if (py > y + h - 2) py = y + h - 2;
    int16_t px = x + 1 + i;
    if (i > 0) tft.drawLine(px - 1, prevY, px, py, color);
    prevY = py;
  }
}

void gambarLayoutGrafik() {
  tft.fillScreen(TFT_BLACK);
  perbaruiTampilanGrafik(true);
}

void perbaruiTampilanGrafik(bool forceRedraw) {
  static unsigned long lastSample = 0;
  unsigned long now = millis();
  bool sampel = (now - lastSample >= 100);
  if (!forceRedraw && !sampel) return;

  if (sampel) {
    lastSample = now;
    float busvoltage = 0.0;
    float current_A = 0.0;
    float power_W = 0.0;

    if (ina226Aktif) {
      busvoltage = INA.getBusVoltage();
      current_A  = INA.getCurrent();
      power_W    = INA.getPower();
      hitungParameterListrik(busvoltage, current_A, power_W);
    }

    grafV = busvoltage;
    grafA = current_A;

    for (int i = 0; i < GRAF_W - 1; i++) {
      histV[i] = histV[i + 1];
      histA[i] = histA[i + 1];
    }
    histV[GRAF_W - 1] = busvoltage;
    histA[GRAF_W - 1] = current_A;

    if (cekAutoKembaliKeUtama(current_A)) return; 
  }

  char bufV[10], bufA[10];
  snprintf(bufV, sizeof(bufV), "%06.3f", grafV);
  snprintf(bufA, sizeof(bufA), "%06.3f", grafA);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(bufV, 1, 0, 4);
  tft.drawString("V", 90, 0, 2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(bufA, 1, 24, 4);
  tft.drawString("A", 90, 24, 2);

  gambarGrafik(50, 37, histV, 1.0f, TFT_CYAN, "V");
  gambarGrafik(90, 37, histA, 0.1f, TFT_GREEN, "A");

  gambarPanelKanan();
}

//  Layar pengaturan
void gambarPengaturan() {
  char buf[24];

  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(160);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("PENGATURAN", 1, 0, 2);

  for (uint8_t i = 0; i < SETTING_JUMLAH; i++) {
    switch (i) {
      case 0: snprintf(buf, sizeof(buf), "SKALA CC:%s", CC_STEP_LABEL[ccStepIndex]); break;
      case 1: snprintf(buf, sizeof(buf), "AUTO GIF:%s", autoKembaliAktif ? "ON" : "OFF"); break;
      case 2: snprintf(buf, sizeof(buf), "WAKTU:%d mnt", autoKembaliMenit); break;
      case 3: snprintf(buf, sizeof(buf), "GRAFIK V/A"); break;
      default: snprintf(buf, sizeof(buf), "KEMBALI"); break;
    }

    uint16_t fg = TFT_LIGHTGREY;
    uint16_t bg = TFT_BLACK;
    if (i == settingIndex) {
      if (settingState == SET_EDIT) { fg = TFT_BLACK; bg = TFT_GREEN; }
      else { fg = TFT_WHITE; bg = TFT_DARKGREY; }
    }
    tft.setTextColor(fg, bg);
    tft.drawString(buf, 1, 20 + i * 19, 2);
  }

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  if (settingState == SET_EDIT) tft.drawString("UP/DN:UBAH  OK:BACK", 1, 118, 1);
  else tft.drawString("UP/DN:GERAK OK:PILIH", 1, 118, 1);

  tft.setTextPadding(0);
}

void segarkanTampilan() {
  if (modeTampilan == 1) perbaruiTampilanINA(true);
  else if (modeTampilan == 3) perbaruiTampilanGrafik(true);
  else if (modeTampilan == 2) gambarPengaturan();
}

void gambarUlangTampilan() {
  if (modeTampilan == 1) {
    gambarLayoutINA();
  } else if (modeTampilan == 3) {
    gambarLayoutGrafik();
  } else if (modeTampilan == 2) {
    tft.fillScreen(TFT_BLACK);
    gambarPengaturan();
  } else {
    tft.fillScreen(TFT_BLACK);
  }
}

void toggleWifi() {
  wifiAktif = !wifiAktif;
  if (wifiAktif) {
    WiFi.forceSleepWake();
    delay(10);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    server.begin();
    Serial.println(">>> WIFI DIAKTIFKAN <<<");
  } else {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.enableAP(false);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(10);
    Serial.println(">>> WIFI DIMATIKAN <<<");
  }

  gambarUlangTampilan();
}

void ubahStatusMosfet() {
  tripOccurred = false;
  mosfetState = !mosfetState;

  if (limitMode == 2) {
    pwmDuty = 0; // Soft-start dimulai dari 0 saat diaktifkan
  } else {
    pwmDuty = mosfetState ? 1023 : 0;
  }

  digitalWrite(MOSFET_PIN, mosfetState ? HIGH : LOW);
  segarkanTampilan();

  Serial.println("Tombol OK: MOSFET Status = " + String(mosfetState ? "ON" : "OFF"));
}

void keTampilanPengukur() {
  modeTampilan = 1;
  menuState = MENU_TUTUP;
  if (menuIndex >= MENU_JUMLAH) menuIndex = 0;
  gif.close();
  if (!ina226Aktif) ina226Aktif = inisialisasiINA226();
  gambarLayoutINA();
  Serial.println(">>> BERPINDAH KE MODE PENGUKUR <<<");
}

void keTampilanPengaturan() {
  modeTampilan = 2;
  menuState = MENU_TUTUP;
  settingState = SET_PILIH;
  settingIndex = 0;
  gif.close();
  tft.fillScreen(TFT_BLACK);
  gambarPengaturan();
  Serial.println(">>> BERPINDAH KE PENGATURAN <<<");
}

void keTampilanGrafik() {
  modeTampilan = 3;
  menuState = MENU_TUTUP;
  menuIndex = 0;
  gif.close();
  if (!ina226Aktif) ina226Aktif = inisialisasiINA226();
  for (int i = 0; i < GRAF_W; i++) {
    histV[i] = 0.0;
    histA[i] = 0.0;
  }
  gambarLayoutGrafik();
  Serial.println(">>> BERPINDAH KE MODE GRAFIK <<<");
}

void keTampilanGIF() {
  modeTampilan = 0;
  menuState = MENU_TUTUP;
  tft.fillScreen(TFT_BLACK);
  if (LittleFS.exists(currentGifPath)) {
    adaGif = true;
  } else {
    adaGif = false;
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Gak ada GIF", tft.width() / 2, tft.height() / 2, 2);
  }
  Serial.println(">>> BERPINDAH KE MODE GIF <<<");
}

void ubahNilaiMenu(int8_t arah) {
  switch (menuIndex) {
    case 0: { // CC (langkah sesuai skala di pengaturan)
      float step = CC_STEP[ccStepIndex];
      currentLimit_A = roundf((currentLimit_A + arah * step) * 100.0f) / 100.0f;
      if (currentLimit_A > 20.0) currentLimit_A = 20.0;
      if (currentLimit_A < 0.0) currentLimit_A = 0.0;
      tripOccurred = false;
      break;
    }

    case 1: // MODE
      limitMode = (arah > 0) ? 2 : 1;
      if (limitMode == 2) {
        pwmDuty = 0;
      } else {
        pwmDuty = mosfetState ? 1023 : 0;
        digitalWrite(MOSFET_PIN, mosfetState ? HIGH : LOW);
      }
      break;

    case 2: // WIFI
      if (arah > 0 && !wifiAktif) toggleWifi();
      else if (arah < 0 && wifiAktif) toggleWifi();
      break;

    default:
      break;
  }
}

void ubahNilaiSetting(int8_t arah) {
  switch (settingIndex) {
    case 0: // skala set CC: UP = langkah lebih besar, DOWN = lebih halus
      if (arah > 0 && ccStepIndex > 0) ccStepIndex--;
      else if (arah < 0 && ccStepIndex < 2) ccStepIndex++;
      break;

    case 1:
      autoKembaliAktif = (arah > 0);
      break;

    case 2:
      if (arah > 0 && autoKembaliMenit < 60) autoKembaliMenit++;
      else if (arah < 0 && autoKembaliMenit > 1) autoKembaliMenit--;
      break;

    default:
      break;
  }
  mulaiTanpaArus = millis();
}

void prosesOKPengaturan() {
  switch (settingIndex) {
    case 0:
    case 1:
    case 2:
      settingState = (settingState == SET_PILIH) ? SET_EDIT : SET_PILIH;
      gambarPengaturan();
      break;
    case 3:
      keTampilanGrafik();
      break;
    default:
      keTampilanPengukur();
      break;
  }
}

void prosesOKPendek() {
  if (modeTampilan == 0) {
    keTampilanPengukur();
    return;
  }

  if (modeTampilan == 2) {
    prosesOKPengaturan();
    return;
  }

  if (menuState == MENU_TUTUP) {
    ubahStatusMosfet();             
  } else if (menuState == MENU_PILIH) {
    switch (menuIndex) {
      case 3: keTampilanPengaturan(); break;   
      case 4: keTampilanGIF(); break;         
      case 5: keTampilanPengukur(); break;    
      default:
        menuState = MENU_EDIT;       
        segarkanTampilan();
        break;
    }
  } else {
    menuState = MENU_PILIH;         
    segarkanTampilan();
  }
}


void prosesUpPanjang() {
  if (modeTampilan != 1 && modeTampilan != 3) return;

  if (menuState == MENU_TUTUP) {
    menuState = MENU_PILIH;           
    Serial.println(">>> MODE MENU <<<");
  } else if (menuState == MENU_PILIH) {
    menuState = MENU_TUTUP;           
    Serial.println(">>> MODE BEBAN <<<");
  } else {
    return;                          
  }
  segarkanTampilan();
}

void prosesUp() {
  if (modeTampilan == 2) {
    if (settingState == SET_PILIH) {
      settingIndex = (settingIndex + SETTING_JUMLAH - 1) % SETTING_JUMLAH;
    } else {
      ubahNilaiSetting(+1);
    }
    gambarPengaturan();
    return;
  }

  if (modeTampilan != 1 && modeTampilan != 3) return;

  if (menuState == MENU_PILIH) {
    uint8_t n = jumlahMenu();
    menuIndex = (menuIndex + n - 1) % n;
  } else if (menuState == MENU_EDIT) {
    ubahNilaiMenu(+1);
  } else {
    return;
  }
  segarkanTampilan();
}

void prosesDown() {
  if (modeTampilan == 2) {
    if (settingState == SET_PILIH) {
      settingIndex = (settingIndex + 1) % SETTING_JUMLAH;
    } else {
      ubahNilaiSetting(-1);
    }
    gambarPengaturan();
    return;
  }

  if (modeTampilan != 1 && modeTampilan != 3) return;

  if (menuState == MENU_PILIH) {
    menuIndex = (menuIndex + 1) % jumlahMenu();
  } else if (menuState == MENU_EDIT) {
    ubahNilaiMenu(-1);
  } else {
    return;
  }
  segarkanTampilan();
}

bool cekAutoKembaliKeUtama(float current_A) {
  if ((modeTampilan != 1 && modeTampilan != 3) || menuState != MENU_TUTUP || !autoKembaliAktif) {
    mulaiTanpaArus = millis();
    return false;
  }

  if (current_A > 0.010) {
    mulaiTanpaArus = millis();
    return false;
  }

  if (mulaiTanpaArus == 0) mulaiTanpaArus = millis();
  if (millis() - mulaiTanpaArus >= (unsigned long)autoKembaliMenit * 60000UL) {
    keTampilanGIF();
    mulaiTanpaArus = millis();
    Serial.println(">>> AUTO KEMBALI KE TAMPILAN UTAMA <<<");
    return true;
  }
  return false;
}

void cekTombol() {
  unsigned long now = millis();
  if (now - lastButtonScan < 10) return;
  lastButtonScan = now;

  int okState = digitalRead(BUTTON_OK);
  int upState = digitalRead(BUTTON_UP);
  int downState = digitalRead(BUTTON_DOWN);

  static bool lastOK = HIGH;
  static bool lastDown = HIGH;

  if (okState == LOW && lastOK == HIGH) prosesOKPendek();

  if (upState == LOW && !isPressingUP) {
    isPressingUP = true;
    pressStartUP = now;
    longPressUPExecuted = false;
  }

  if (upState == LOW && isPressingUP && !longPressUPExecuted &&
      (now - pressStartUP >= LONG_PRESS_TIME)) {
    longPressUPExecuted = true;
    prosesUpPanjang();
  }

  if (upState == HIGH && isPressingUP) {
    isPressingUP = false;
    if (!longPressUPExecuted) prosesUp();
  }

  if (downState == LOW && lastDown == HIGH) prosesDown();

  lastOK = okState;
  lastDown = downState;
}

void setup() {
  system_update_cpu_freq(160);
  Serial.begin(115200);

  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW); 
  pinMode(BUTTON_OK, INPUT_PULLUP);
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(ALERT_PIN, INPUT_PULLUP);
  tft.init();
  tft.setRotation(3); 
  tft.setSwapBytes(true);
  tft.invertDisplay(false); 
  tft.fillScreen(TFT_BLACK);

  Wire.begin(); 
  ina226Aktif = inisialisasiINA226();

  if (!LittleFS.begin()) {
    Serial.println("Gagal Mount LittleFS!");
    return;
  }

  gif.begin(GIF_PALETTE_RGB565_LE);
  wifiAktif = false;
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/setLimit", HTTP_GET, handleSetLimit);
  server.on("/toggleAutoReset", HTTP_GET, handleToggleAutoReset);
  server.on("/toggleMosfet", HTTP_GET, handleToggleMosfet);
  server.on("/upload", HTTP_POST, handleUploadResponse, handleFileUpload);
  server.client().setTimeout(500); 
  modeTampilan = 0;

  if (LittleFS.exists(currentGifPath)) {
    adaGif = true;
    tft.fillScreen(TFT_BLACK);
  } else {
    adaGif = false;
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Tekan OK untuk pengukur", 80, 50, 2);
    tft.drawString("Tahan UP 3 detik = menu", 80, 70, 1);
  }
}

void loop() {
  cekTombol();
  
  if (wifiAktif) {
    server.handleClient();
  }

  if (modeTampilan == 0) {
    proteksiLatar();

    if (adaGif) {
      gifBerubah = false;
      if (gif.open(currentGifPath.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
        hitungSkalaGIF(gif.getCanvasWidth(), gif.getCanvasHeight());

        if (uploadSelesai) {
          tft.fillScreen(TFT_BLACK);
          uploadSelesai = false;
        }

        while (gif.playFrame(true, NULL)) {
          if (wifiAktif) gambarIkonWifi(142, 2);
          yield();
          cekTombol();
          proteksiLatar();
          if (wifiAktif) server.handleClient();
          if (modeTampilan != 0 || gifBerubah) break;
        }
        gif.close();
      }
    }
  } else if (modeTampilan == 1) {
    perbaruiTampilanINA();
  } else if (modeTampilan == 2) {
    proteksiLatar();              
  } else if (modeTampilan == 3) {
    perbaruiTampilanGrafik();
  }
}
