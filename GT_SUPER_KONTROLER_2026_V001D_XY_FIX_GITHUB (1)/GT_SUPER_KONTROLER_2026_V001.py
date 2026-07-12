import json
import queue
import threading
import time
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox

try:
    import serial
    from serial.tools import list_ports
except Exception:
    serial = None
    list_ports = None

APP_TITLE = "GT_SUPER KONTROLER 2026 V001"
BASE_DIR = Path(__file__).resolve().parent
SETTINGS_FILE = BASE_DIR / "GT_SUPER_KONTROLER_2026_V001_ayarlar.json"
LOG_FILE = BASE_DIR / "AYAR_KAYITLARI.txt"
SERVIS_BILGISI = "İletişim / Servis: GÖRKEM TURAN | Telefon / WhatsApp: +995557880008"

HID_KEYS = {
    "KAPALI": 0x00,
    "1": 0x1E, "2": 0x1F, "3": 0x20, "4": 0x21, "5": 0x22,
    "6": 0x23, "7": 0x24, "8": 0x25, "9": 0x26, "0": 0x27,
    "A": 0x04, "B": 0x05, "C": 0x06, "D": 0x07, "E": 0x08, "F": 0x09,
    "G": 0x0A, "H": 0x0B, "I": 0x0C, "J": 0x0D, "K": 0x0E, "L": 0x0F,
    "M": 0x10, "N": 0x11, "O": 0x12, "P": 0x13, "Q": 0x14, "R": 0x15,
    "S": 0x16, "T": 0x17, "U": 0x18, "V": 0x19, "W": 0x1A, "X": 0x1B,
    "Y": 0x1C, "Z": 0x1D,
    "ENTER": 0x28, "SPACE": 0x2C, "ESC": 0x29,
}
KEY_BY_CODE = {v: k for k, v in HID_KEYS.items()}
KEY_CHOICES = list(HID_KEYS.keys())
PIN_CHOICES = [str(x) for x in [2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,26,27,28,255]]

ROLE_AUTO = "AUTO"
ROLE_P1 = "P1_GUN"
ROLE_P2 = "P2_GUN"
ROLE_CTRL = "CONTROLLER"
ROLE_CHOICES = [
    (ROLE_AUTO, "Otomatik tanı"),
    (ROLE_P1, "Oyuncu 1 Silah"),
    (ROLE_P2, "Oyuncu 2 Silah"),
    (ROLE_CTRL, "Kontrol Kartı / Kredi-Röle"),
]
ROLE_LABELS = dict(ROLE_CHOICES)
ROLE_BY_LABEL = {v: k for k, v in ROLE_CHOICES}
ROLE_LABEL_LIST = [v for _, v in ROLE_CHOICES]

CTRL_LABELS = {
    "CREDIT_GP": "Kredi / Coin GP",
    "COIN_AH": "Kredi Tercihi",
    "P1_START": "P1 Start GP",
    "P2_START": "P2 Start GP",
    "P1_TRIG": "P1 Tetik GP",
    "P2_TRIG": "P2 Tetik GP",
    "P1_RELOAD": "P1 Bomba GP",
    "P2_RELOAD": "P2 Bomba GP",
    "P1_RELAY": "Röle 1 GP",
    "P2_RELAY": "Röle 2 GP",
    "IDLE_MIN": "Boşta Süre (dk)",
    "REL_AH": "Röle Tercihi (otomatik)",
    "RELAY_MODE": "Röle Çalışma Şekli",
    "P1_VIB": "P1 Titreşim GP (Yok)",
    "P2_VIB": "P2 Titreşim GP (Yok)",
    "CAL_GP": "Kalibrasyon GP (Yok)",
    "CTRL_ENABLE_GP": "Kontrol Aktif GP (Yok)",
    "VIB_AH": "Titreşim Yönü (Yok)",
    "VIB_MODE": "Titreşim Modu (Yok)",
    "KEY_COIN": "Coin Tuşu",
    "KEY_P1_START": "P1 Start Tuşu",
    "KEY_P2_START": "P2 Start Tuşu",
    "KEY_P1_TRIG": "P1 Tetik Tuşu",
    "KEY_P1_RELOAD": "P1 Bomba Tuşu",
    "KEY_P2_TRIG": "P2 Tetik Tuşu",
    "KEY_P2_RELOAD": "P2 Bomba Tuşu",
}
MOUSE_LABELS = {
    "XMIN": "X Sol Sınır", "XMAX": "X Sağ Sınır", "YMIN": "Y Üst Sınır", "YMAX": "Y Alt Sınır",
    "INVX": "X Ters", "INVY": "Y Ters", "FILTER": "Titreşim Azaltma", "THR": "Titreme Eşiği",
    "ENABLE_GP": "GP19 DIP Aktif", "CAL_GP": "Kalibrasyon GP", "EDGE_L": "Sol Kenar", "EDGE_R": "Sağ Kenar",
    "EDGE_T": "Üst Kenar", "EDGE_B": "Alt Kenar",
}
CTRL_HELP = {
    "Kredi Tercihi": "LOW seçilirse kredi ve röle LOW/GND ile çalışır. HIGH seçilirse ikisi de HIGH/3.3V ile çalışır.",
    "Boşta Süre (dk)": "Dakika olarak hareketsizlik süresi.",
}

# Seçenekli alanlar: ekranda Türkçe görünür, cihaza yazarken 0/1 olarak gönderilir.
CONTROL_OPTIONS = {
    "COIN_AH": {
        "0": "LOW / GND ile çalışsın (0) - Coin GND’ye çekince sayar, röle LOW ile çeker",
        "1": "HIGH / 3.3V ile çalışsın (1) - Coin HIGH görünce sayar, röle HIGH ile çeker",
    },
}
OPTION_VALUE_BY_LABEL = {k: {label: val for val, label in opts.items()} for k, opts in CONTROL_OPTIONS.items()}

def option_label(key, value):
    s = str(value)
    if key in CONTROL_OPTIONS:
        if s in CONTROL_OPTIONS[key]:
            return CONTROL_OPTIONS[key][s]
    return s

def option_value(key, value):
    s = str(value)
    if key in CONTROL_OPTIONS:
        if s in OPTION_VALUE_BY_LABEL[key]:
            return OPTION_VALUE_BY_LABEL[key][s]
        if s in CONTROL_OPTIONS[key]:
            return s
        # Eski yerel kayıtlardan gelirse içindeki (0) / (1) ifadesini yakala.
        if "(0)" in s or s.strip().endswith("0"):
            return "0"
        if "(1)" in s or s.strip().endswith("1"):
            return "1"
    return s

DEFAULT_CONTROLLER = {
    "CREDIT_GP": 2,
    "COIN_AH": 0,
    "P1_START": 3,
    "P2_START": 6,
    "P1_TRIG": 4,
    "P2_TRIG": 7,
    "P1_RELOAD": 5,
    "P2_RELOAD": 8,
    "P1_RELAY": 27,
    "P2_RELAY": 26,
    "P1_VIB": 255,
    "P2_VIB": 255,
    "CAL_GP": 255,
    "CTRL_ENABLE_GP": 255,
    "IDLE_MIN": 5,
    "REL_AH": 0,
    "VIB_AH": 1,
    "RELAY_MODE": 0,
    "VIB_MODE": 0,
    "KEY_COIN": HID_KEYS["1"],
    "KEY_P1_START": HID_KEYS["2"],
    "KEY_P2_START": HID_KEYS["5"],
    "KEY_P1_TRIG": HID_KEYS["3"],
    "KEY_P1_RELOAD": HID_KEYS["4"],
    "KEY_P2_TRIG": HID_KEYS["6"],
    "KEY_P2_RELOAD": HID_KEYS["7"],
}
DEFAULT_MOUSE = {
    "XMIN": 200, "XMAX": 3900, "YMIN": 200, "YMAX": 3900,
    "INVX": 0, "INVY": 0, "FILTER": 4, "THR": 12,
    "ENABLE_GP": 19, "CAL_GP": 255,
    "EDGE_L": 0, "EDGE_R": 0, "EDGE_T": 0, "EDGE_B": 0,
}

CAL_STEPS = [
    ("LU", "1. Sol Üst Köşe"),
    ("RU", "2. Sağ Üst Köşe"),
    ("RD", "3. Sağ Alt Köşe"),
    ("LD", "4. Sol Alt Köşe"),
]

class Device:
    def __init__(self, port):
        self.port = port
        self.kind = "UNKNOWN"
        self.player = ""
        self.ser = None
        self.q = queue.Queue()
        self.lines = []
        self.last_status = ""
        self.cfg = {}
        self.running = False

    def open(self):
        self.ser = serial.Serial(self.port, 115200, timeout=0.05)
        self.running = True
        threading.Thread(target=self._read_loop, daemon=True).start()
        self.send("HELLO")
        self.send("GETCFG")

    def close(self):
        self.running = False
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass

    def send(self, line):
        try:
            self.ser.write((line.strip()+"\n").encode("ascii", errors="ignore"))
        except Exception:
            pass

    def _read_loop(self):
        while self.running:
            try:
                line = self.ser.readline().decode("ascii", errors="ignore").strip()
                if line:
                    self.parse(line)
            except Exception:
                time.sleep(0.05)

    def parse(self, line):
        self.lines.append(line)
        if len(self.lines) > 50:
            self.lines = self.lines[-50:]
        if line.startswith("HELLO,CONTROLLER") or line.startswith("CFG,CONTROLLER") or line.startswith("STATUS,CONTROLLER"):
            self.kind = "CONTROLLER"; self.player = "CTRL"
        elif ",MOUSE,P1" in line:
            self.kind = "MOUSE"; self.player = "P1"
        elif ",MOUSE,P2" in line:
            self.kind = "MOUSE"; self.player = "P2"
        if line.startswith("CFG,"):
            self.cfg.update(parse_pairs(line.split(",")[3:]))
        if line.startswith("STATUS,"):
            self.last_status = line
        self.q.put(line)


def parse_pairs(parts):
    out = {}
    i = 0
    while i + 1 < len(parts):
        k, v = parts[i], parts[i+1]
        try:
            out[k] = int(v)
        except Exception:
            out[k] = v
        i += 2
    return out

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1280x900")
        self.attributes("-fullscreen", True)
        self.bind("<Escape>", lambda e: self.attributes("-fullscreen", False))
        self.bind("<F11>", self.toggle_fullscreen)
        self.bind("<ButtonRelease-1>", self.handle_calibration_mouse_click)
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.devices = {}
        self.ctrl_vars = {k: tk.StringVar(value=str(v)) for k, v in DEFAULT_CONTROLLER.items()}
        self.mouse_vars = {
            "P1": {k: tk.StringVar(value=str(v)) for k, v in DEFAULT_MOUSE.items()},
            "P2": {k: tk.StringVar(value=str(v)) for k, v in DEFAULT_MOUSE.items()},
        }
        self.key_vars = {
            "KEY_COIN": tk.StringVar(value="1"),
            "KEY_P1_START": tk.StringVar(value="2"),
            "KEY_P2_START": tk.StringVar(value="5"),
            "KEY_P1_TRIG": tk.StringVar(value="3"),
            "KEY_P1_RELOAD": tk.StringVar(value="4"),
            "KEY_P2_TRIG": tk.StringVar(value="6"),
            "KEY_P2_RELOAD": tk.StringVar(value="7"),
        }
        self.capture = {"P1": {}, "P2": {}}
        self.cal_active = {"P1": False, "P2": False}
        self.cal_index = {"P1": 0, "P2": 0}
        self.cal_start_ms = {"P1": 0, "P2": 0}
        self.cal_step_vars = {
            "P1": tk.StringVar(value="Kalibrasyon bekliyor"),
            "P2": tk.StringVar(value="Kalibrasyon bekliyor"),
        }
        self.armed_capture = None  # eski uyumluluk; yeni sistem sıralı kalibrasyon kullanır
        self._last_t1 = 0
        self._last_t2 = 0
        self.role_assignments = {}  # COM port -> AUTO / P1_GUN / P2_GUN / CONTROLLER
        self.device_tree = None
        self.selected_port_var = tk.StringVar(value="Seçili cihaz yok")
        self.selected_role_var = tk.StringVar(value=ROLE_LABELS[ROLE_AUTO])
        self.load_local()
        self.normalize_option_vars()
        self._fullscreen = True
        self.create_ui()
        self.after(400, self.refresh)

    def toggle_fullscreen(self, event=None):
        self._fullscreen = not getattr(self, "_fullscreen", True)
        self.attributes("-fullscreen", self._fullscreen)

    def load_local(self):
        if SETTINGS_FILE.exists():
            try:
                data = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
                for k,v in data.get("controller", {}).items():
                    if k in self.ctrl_vars: self.ctrl_vars[k].set(str(v))
                for p in ("P1","P2"):
                    for k,v in data.get(p, {}).items():
                        if k in self.mouse_vars[p]: self.mouse_vars[p][k].set(str(v))
                for k,v in data.get("keys", {}).items():
                    if k in self.key_vars: self.key_vars[k].set(str(v))
                self.role_assignments = dict(data.get("roles", {}))
            except Exception:
                pass

    def normalize_option_vars(self):
        for k in CONTROL_OPTIONS:
            if k in self.ctrl_vars:
                self.ctrl_vars[k].set(option_label(k, self.ctrl_vars[k].get()))

    def save_local(self, action=None):
        data = {
            "controller": {k:v.get() for k,v in self.ctrl_vars.items()},
            "P1": {k:v.get() for k,v in self.mouse_vars["P1"].items()},
            "P2": {k:v.get() for k,v in self.mouse_vars["P2"].items()},
            "keys": {k:v.get() for k,v in self.key_vars.items()},
            "roles": self.role_assignments,
        }
        SETTINGS_FILE.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        if action:
            self.log_current_settings(action)

    def create_ui(self):
        try:
            self.style = ttk.Style(self)
            if "clam" in self.style.theme_names():
                self.style.theme_use("clam")
            self.configure(bg="#0f172a")
            self.style.configure("TFrame", background="#dbeafe")
            self.style.configure("TLabelframe", background="#eff6ff", borderwidth=2, relief="solid")
            self.style.configure("TLabelframe.Label", font=("Segoe UI", 12, "bold"), foreground="#1e3a8a", background="#eff6ff")
            self.style.configure("Title.TLabel", font=("Segoe UI", 26, "bold"), foreground="#0f172a", background="#dbeafe")
            self.style.configure("Sub.TLabel", font=("Segoe UI", 12, "bold"), foreground="#1d4ed8", background="#dbeafe")
            self.style.configure("TNotebook", background="#bfdbfe", borderwidth=0)
            self.style.configure("TNotebook.Tab", font=("Segoe UI", 11, "bold"), padding=(14, 10), background="#93c5fd", foreground="#0f172a")
            self.style.map("TNotebook.Tab", background=[("selected", "#2563eb")], foreground=[("selected", "white")])
            self.style.configure("Accent.TButton", font=("Segoe UI", 12, "bold"), padding=10, background="#2563eb", foreground="white")
            self.style.map("Accent.TButton", background=[("active", "#1d4ed8")], foreground=[("active", "white")])
            self.style.configure("Green.TButton", font=("Segoe UI", 12, "bold"), padding=10, background="#16a34a", foreground="white")
            self.style.map("Green.TButton", background=[("active", "#15803d")], foreground=[("active", "white")])
            self.style.configure("Red.TButton", font=("Segoe UI", 12, "bold"), padding=10, background="#dc2626", foreground="white")
            self.style.map("Red.TButton", background=[("active", "#b91c1c")], foreground=[("active", "white")])
            self.style.configure("Treeview", background="#ffffff", fieldbackground="#ffffff", rowheight=26)
            self.style.configure("Treeview.Heading", font=("Segoe UI", 10, "bold"), background="#60a5fa", foreground="#0f172a")
        except Exception:
            pass
        top = ttk.Frame(self, padding=(14,10)); top.pack(fill="x")
        left = ttk.Frame(top); left.pack(side="left", fill="x", expand=True)
        ttk.Label(left, text=APP_TITLE, style="Title.TLabel").pack(anchor="w")
        ttk.Label(left, text="İletişim / Servis: GÖRKEM TURAN  |  Telefon / WhatsApp: +995557880008", style="Sub.TLabel").pack(anchor="w")
        ttk.Label(left, text=SERVIS_BILGISI, style="Sub.TLabel").pack(anchor="w")
        ttk.Button(top, text="Cihazları Tara / Yenile", style="Accent.TButton", command=self.scan).pack(side="right", padx=4)
        ttk.Button(top, text="Bilgisayara Kaydet", command=lambda: self.save_local("Bilgisayara Kaydet"), style="Green.TButton").pack(side="right", padx=4)
        ttk.Button(top, text="Tam Ekrandan Çık", command=lambda: self.attributes("-fullscreen", False), style="Red.TButton").pack(side="right", padx=4)
        self.status = ttk.Label(self, text="GT_SUPER KONTROLER 2026 V001 hazır. 3 Pico'yu tak, Cihazları Tara ve görevleri seç. ESC tam ekrandan çıkar. Ayarlar tarih ve saat ile kaydedilir.", padding=(12,10), relief="groove", font=("Segoe UI", 11, "bold"), background="#fef3c7", foreground="#7c2d12")
        self.status.pack(fill="x", padx=10)
        self.tabs = ttk.Notebook(self); self.tabs.pack(fill="both", expand=True, padx=10, pady=10)
        self.tab_devices = ttk.Frame(self.tabs); self.tab_ctrl = ttk.Frame(self.tabs); self.tab_p1 = ttk.Frame(self.tabs); self.tab_p2 = ttk.Frame(self.tabs); self.tab_tests = ttk.Frame(self.tabs); self.tab_help = ttk.Frame(self.tabs)
        self.tabs.add(self.tab_devices, text="Cihaz Durumu")
        self.tabs.add(self.tab_ctrl, text="Kontrol Kartı Ayarları")
        self.tabs.add(self.tab_p1, text="Oyuncu 1 Kalibrasyon")
        self.tabs.add(self.tab_p2, text="Oyuncu 2 Kalibrasyon")
        self.tabs.add(self.tab_tests, text="Testler")
        self.tabs.add(self.tab_help, text="Yardım")
        self.build_devices_tab(); self.build_ctrl_tab(); self.build_mouse_tab("P1", self.tab_p1); self.build_mouse_tab("P2", self.tab_p2); self.build_tests_tab(); self.build_help_tab()

    def build_devices_tab(self):
        outer = ttk.Frame(self.tab_devices, padding=10)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text="Cihaz Tarama ve Görev Atama", font=("Segoe UI", 14, "bold")).pack(anchor="w")
        ttk.Label(outer, text="Taramadan sonra her COM cihazı için görev seç: Oyuncu 1 Silah, Oyuncu 2 Silah veya Kontrol Kartı.").pack(anchor="w", pady=(0,8))

        body = ttk.Frame(outer)
        body.pack(fill="both", expand=True)

        columns = ("port", "detected", "assigned", "effective", "status")
        self.device_tree = ttk.Treeview(body, columns=columns, show="headings", height=10)
        headings = {
            "port": "COM Port",
            "detected": "Cihazın Kendini Tanıtması",
            "assigned": "Seçilen Görev",
            "effective": "Programın Kullanacağı Görev",
            "status": "Son Durum",
        }
        widths = {"port": 100, "detected": 180, "assigned": 170, "effective": 190, "status": 430}
        for c in columns:
            self.device_tree.heading(c, text=headings[c])
            self.device_tree.column(c, width=widths[c], anchor="w")
        self.device_tree.pack(side="left", fill="both", expand=True)
        self.device_tree.bind("<<TreeviewSelect>>", self.on_device_select)
        sb = ttk.Scrollbar(body, orient="vertical", command=self.device_tree.yview)
        sb.pack(side="left", fill="y")
        self.device_tree.configure(yscrollcommand=sb.set)

        side = ttk.LabelFrame(body, text="Seçili cihazı ne yapacağız?", padding=10)
        side.pack(side="right", fill="y", padx=(10,0))
        ttk.Label(side, textvariable=self.selected_port_var, font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(0,8))
        ttk.Label(side, text="Görev seç:").pack(anchor="w")
        ttk.Combobox(side, textvariable=self.selected_role_var, values=ROLE_LABEL_LIST, state="readonly", width=26).pack(anchor="w", pady=(2,8))
        ttk.Button(side, text="Seçili Cihaza Görevi Ata", command=self.assign_selected_role).pack(anchor="w", fill="x", pady=3)
        ttk.Button(side, text="Kimlik / Ayar Oku", command=self.request_selected_cfg).pack(anchor="w", fill="x", pady=3)
        ttk.Button(side, text="Rol Atamalarını Temizle", command=self.clear_role_assignments).pack(anchor="w", fill="x", pady=3)
        ttk.Separator(side).pack(fill="x", pady=10)
        ttk.Label(side, text="Kural:", font=("Segoe UI", 10, "bold")).pack(anchor="w")
        ttk.Label(side, text="• P1 silah = P1 kalibrasyon sekmesi\n• P2 silah = P2 kalibrasyon sekmesi\n• Controller = GP2 coin, start, tetik-röle\n• Otomatik tanı = cihazın HELLO cevabı", justify="left").pack(anchor="w")

        logbox = ttk.LabelFrame(outer, text="Cihaz Günlüğü", padding=6)
        logbox.pack(fill="both", expand=True, pady=(10,0))
        self.dev_text = tk.Text(logbox, height=8, font=("Consolas", 10), wrap="none")
        self.dev_text.pack(fill="both", expand=True)

    def combo_or_entry(self, parent, var, values=None, width=10):
        if values:
            return ttk.Combobox(parent, textvariable=var, values=values, width=width, state="readonly")
        return ttk.Entry(parent, textvariable=var, width=width)

    def build_ctrl_tab(self):
        f = ttk.Frame(self.tab_ctrl, padding=10); f.pack(fill="both", expand=True)
        ttk.Label(f, text="Kontrol Kartı Ayarları", font=("Segoe UI", 18, "bold"), foreground="#1d4ed8").grid(row=0, column=0, columnspan=6, sticky="w", pady=(0,8))
        ttk.Label(f, text="Standart pinler: GP2 Coin, GP3 P1 Start, GP4 P1 Tetik, GP5 P1 Bomba, GP6 P2 Start, GP7 P2 Tetik, GP8 P2 Bomba. Klavye karakterleri 1-7 standarttır.").grid(row=1, column=0, columnspan=6, sticky="w", pady=(0,4))
        ttk.Label(f, text="Röle çalışma şekli: Kredi + Start ile oyuncu hazır olur; aktif oyuncu tetiğe basınca röle çeker, bırakınca bırakır; boşta süre dolunca pasif olur.", foreground="#7c2d12", font=("Segoe UI", 11, "bold")).grid(row=2, column=0, columnspan=6, sticky="w", pady=(0,8))
        groups = [
            ("Değiştirilebilir Ayarlar", ["COIN_AH", "IDLE_MIN", "P1_RELAY", "P2_RELAY"]),
            ("Standart Bağlantı Pinleri", ["CREDIT_GP", "P1_START", "P2_START", "P1_TRIG", "P2_TRIG", "P1_RELOAD", "P2_RELOAD"]),
        ]
        row = 3
        for title, keys in groups:
            box = ttk.LabelFrame(f, text=title, padding=8); box.grid(row=row, column=0, columnspan=6, sticky="ew", pady=5)
            c = 0
            for k in keys:
                lbl = CTRL_LABELS.get(k, k)
                ttk.Label(box, text=lbl).grid(row=c//4*2, column=(c%4)*2, sticky="w", padx=5)
                if k in CONTROL_OPTIONS:
                    vals = list(CONTROL_OPTIONS[k].values())
                    width = 54
                else:
                    vals = PIN_CHOICES if "GP" in k or k.endswith("START") or k.endswith("TRIG") or k.endswith("RELOAD") or k.endswith("RELAY") or k.endswith("VIB") or k == "CREDIT_GP" else None
                    width = 10
                self.combo_or_entry(box, self.ctrl_vars[k], vals, width=width).grid(row=c//4*2+1, column=(c%4)*2, sticky="w", padx=5)
                c += 1
            row += 1
        keybox = ttk.LabelFrame(f, text="Klavye Tuş Atamaları", padding=8); keybox.grid(row=row, column=0, columnspan=6, sticky="ew", pady=5)
        for i, k in enumerate(self.key_vars):
            ttk.Label(keybox, text=CTRL_LABELS.get(k, k)).grid(row=0, column=i, padx=4, sticky="w")
            ttk.Combobox(keybox, textvariable=self.key_vars[k], values=KEY_CHOICES, state="readonly", width=8).grid(row=1, column=i, padx=4)
        row += 1
        btns = ttk.Frame(f); btns.grid(row=row, column=0, columnspan=6, sticky="w", pady=10)
        ttk.Button(btns, text="Cihaza Yaz ve Hafızaya Kaydet", command=self.write_controller, style="Green.TButton").pack(side="left", padx=4)
        ttk.Button(btns, text="Ayar Oku", command=lambda: self.send_to("CONTROLLER", "GETCFG")).pack(side="left", padx=4)
        ttk.Button(btns, text="Fabrika Ayarı", command=lambda: self.send_to("CONTROLLER", "FACTORY")).pack(side="left", padx=4)
        acik = (
            "Açıklama:\n"
            "Kredi Tercihi LOW: Coin butonu GP2'yi GND'ye çekince kredi sayar; röle de LOW/GND ile çeker.\n"
            "Kredi Tercihi HIGH: Coin girişi HIGH/3.3V görünce kredi sayar; röle de HIGH/3.3V ile çeker.\n"
            "Röle çalışma şekli sabittir: kredi + start ile hazır olur; tetik basılıyken çeker; bırakınca bırakır.\n"
            "Boşta Süre dolunca oyuncu pasif olur ve röle çekmez. Tekrar kredi/start gerekir.\n"
            "Her kayıt AYAR_KAYITLARI.txt dosyasına tarih, saat ve değerlerle yazılır."
        )
        ttk.Label(f, text=acik, justify="left", foreground="#334155").grid(row=row+1, column=0, columnspan=6, sticky="w", pady=(6,0))

    def build_mouse_tab(self, player, parent):
        f = ttk.Frame(parent, padding=10); f.pack(fill="both", expand=True)
        baslik = "Oyuncu 1" if player == "P1" else "Oyuncu 2"
        ttk.Label(f, text=f"{baslik} Sıralı 4 Köşe Kalibrasyon", font=("Segoe UI", 18, "bold"), foreground="#1d4ed8").pack(anchor="w")
        ttk.Label(
            f,
            text="Sadece Kalibrasyona Başlat var. Başladıktan sonra sıradaki köşeyi mouse tıklaması veya silah tetiği otomatik onaylar.",
            font=("Segoe UI", 11, "bold"),
            foreground="#0f172a",
        ).pack(anchor="w", pady=(0,8))

        calbox = ttk.LabelFrame(f, text="Kalibrasyon Akışı", padding=10)
        calbox.pack(fill="x", pady=8)
        ttk.Label(calbox, textvariable=self.cal_step_vars[player], font=("Segoe UI", 18, "bold"), foreground="#b91c1c").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0,8))
        ttk.Button(calbox, text="Kalibrasyona Başlat", command=lambda p=player: self.start_calibration_flow(p), style="Green.TButton").grid(row=1, column=0, padx=5, pady=4, sticky="ew")
        ttk.Button(calbox, text="Kalibrasyonu İptal Et", command=lambda p=player: self.cancel_calibration_flow(p), style="Red.TButton").grid(row=1, column=1, padx=5, pady=4, sticky="ew")
        for c in range(2):
            calbox.columnconfigure(c, weight=1)

        ttk.Label(
            calbox,
            text="Onaylama: Kalibrasyon açıkken mouse ile ekrana bir kez tıkla veya ilgili oyuncunun tetiğine bas. Ayrı Mouse/Tetik onay düğmesi yoktur.",
            foreground="#334155",
        ).grid(row=2, column=0, columnspan=2, sticky="w", pady=(8,0))

        box = ttk.LabelFrame(f, text="Mouse Ayarları", padding=8); box.pack(fill="x", pady=8)
        keys = list(DEFAULT_MOUSE.keys())
        for i,k in enumerate(keys):
            ttk.Label(box, text=MOUSE_LABELS.get(k, k)).grid(row=(i//6)*2, column=i%6, padx=4, sticky="w")
            ttk.Entry(box, textvariable=self.mouse_vars[player][k], width=10).grid(row=(i//6)*2+1, column=i%6, padx=4)
        tit = ttk.LabelFrame(f, text="Titreşim Azaltma / Nişangah Titreme", padding=8); tit.pack(fill="x", pady=6)
        ttk.Label(tit, text="Nişangah titriyorsa Güçlü veya Çok Güçlü seç, sonra Mouse Ayarlarını Yaz ve Kaydet.").pack(side="left", padx=4)
        for ad, flt, thr in [("Kapalı",1,2),("Normal",3,8),("Güçlü",4,12),("Çok Güçlü",5,18)]:
            ttk.Button(tit, text=ad, command=lambda p=player, f=flt, t=thr, a=ad: self.set_jitter_level(p,f,t,a)).pack(side="left", padx=4)
        ttk.Button(f, text=f"{baslik} Mouse Ayarlarını Yaz ve Kaydet", command=lambda p=player: self.write_mouse_settings(p), style="Green.TButton").pack(anchor="w", pady=6)
        txt = tk.Text(f, height=10, font=("Consolas", 10)); txt.pack(fill="both", expand=True, pady=8)
        setattr(self, f"{player}_log", txt)

    def build_tests_tab(self):
        f = ttk.Frame(self.tab_tests, padding=12); f.pack(fill="both", expand=True)
        ttk.Label(f, text="Testler", font=("Segoe UI", 14, "bold")).pack(anchor="w")
        for label, cmd in [
            ("P1 Röle Test", "TEST,P1_RELAY"), ("P2 Röle Test", "TEST,P2_RELAY"),
            ("P1 Kapat", "P1OFF"), ("P2 Kapat", "P2OFF"), ("Status İste", "STATUS")]:
            ttk.Button(f, text=label, command=lambda c=cmd: self.send_to("CONTROLLER", c)).pack(anchor="w", pady=3)
        self.test_text = tk.Text(f, font=("Consolas",10)); self.test_text.pack(fill="both", expand=True, pady=8)

    def build_help_tab(self):
        msg = """
GT_SUPER KONTROLER 2026 V001 ana kurallar:

İletişim / Servis: GÖRKEM TURAN | Telefon / WhatsApp: +995557880008

1) Program adı: GT_SUPER KONTROLER 2026 V001. ZIP sade tutuldu; gereksiz dosya yok.
2) P1/P2 silah Pico'lar Controller Pico'ya fiziksel bağlı değildir.
3) P1/P2 silah Pico üzerinde GP19 DIP switch vardır: GP19 GND olursa mouse/potans aktif, açıkta kalırsa pasif.
4) GP2 = klavye 1 / Coin.
5) GP3 = klavye 2 / Player 1 Start.
6) GP4 = klavye 3 / Player 1 Tetik.
7) GP5 = klavye 4 / Player 1 Bomba.
8) GP6 = klavye 5 / Player 2 Start.
9) GP7 = klavye 6 / Player 2 Tetik.
10) GP8 = klavye 7 / Player 2 Bomba.
11) Butonlar gerçek klavye gibi çalışır: kısa basış 1 karakter, basılı tutma sürekli karakter.
12) Relay 1 = GP27, Relay 2 = GP26. Gerekirse programdan 26/27 değiştirilebilir.
13) Kredi Tercihi LOW ise coin ve röle LOW/GND ile; HIGH ise HIGH/3.3V ile çalışır.
14) Röle çalışma şekli sabittir: kredi atılır, start basılınca oyuncu hazır olur, tetik basılıyken röle çeker, bırakınca bırakır.
15) Boşta süre dolarsa oyuncu pasif olur ve röle çekmez. Tekrar kredi/start gerekir.
16) Kalibrasyon P1 ve P2 Pico'nun kendi flash hafızasına otomatik kaydedilir.
17) Ayarlar AYAR_KAYITLARI.txt dosyasına tarih-saat ile kaydedilir.
18) Titreşim azaltma için P1/P2 Kalibrasyon sekmesinde Normal/Güçlü/Çok Güçlü seçilebilir.
"""
        t = tk.Text(self.tab_help, wrap="word", font=("Segoe UI", 11)); t.pack(fill="both", expand=True, padx=8, pady=8); t.insert("1.0", msg)

    def scan(self):
        if serial is None:
            messagebox.showerror("Eksik", "pyserial yüklü değil. requirements.txt ile kur.")
            return
        for d in self.devices.values(): d.close()
        self.devices.clear()
        for p in list_ports.comports():
            try:
                d = Device(p.device); d.open(); self.devices[p.device] = d
            except Exception:
                pass
        self.update_device_tree()
        self.status.config(text=f"{len(self.devices)} COM cihaz açıldı. 2 saniye içinde kimlikler okunur. Sonra görev seçebilirsin.")

    def detected_role_code(self, d):
        if d.kind == "CONTROLLER":
            return ROLE_CTRL
        if d.kind == "MOUSE" and d.player == "P1":
            return ROLE_P1
        if d.kind == "MOUSE" and d.player == "P2":
            return ROLE_P2
        return ROLE_AUTO

    def effective_role_code(self, d):
        assigned = self.role_assignments.get(d.port, ROLE_AUTO)
        if assigned and assigned != ROLE_AUTO:
            return assigned
        return self.detected_role_code(d)

    def role_to_target(self, role_code):
        if role_code == ROLE_CTRL:
            return "CONTROLLER"
        if role_code == ROLE_P1:
            return "P1"
        if role_code == ROLE_P2:
            return "P2"
        return "UNKNOWN"

    def get_dev(self, kind_or_player):
        wanted = {"CONTROLLER": ROLE_CTRL, "P1": ROLE_P1, "P2": ROLE_P2}.get(kind_or_player)
        for d in self.devices.values():
            if self.effective_role_code(d) == wanted:
                return d
        return None

    def on_device_select(self, event=None):
        if not self.device_tree:
            return
        sel = self.device_tree.selection()
        if not sel:
            self.selected_port_var.set("Seçili cihaz yok")
            return
        port = sel[0]
        self.selected_port_var.set(f"Seçili cihaz: {port}")
        role = self.role_assignments.get(port, ROLE_AUTO)
        self.selected_role_var.set(ROLE_LABELS.get(role, ROLE_LABELS[ROLE_AUTO]))

    def assign_selected_role(self):
        if not self.device_tree:
            return
        sel = self.device_tree.selection()
        if not sel:
            messagebox.showwarning("Seçim yok", "Önce listeden bir COM cihaz seç.")
            return
        port = sel[0]
        role = ROLE_BY_LABEL.get(self.selected_role_var.get(), ROLE_AUTO)
        self.role_assignments[port] = role
        self.save_local()
        self.update_device_tree()
        self.status.config(text=f"{port} görevi ayarlandı: {ROLE_LABELS.get(role, role)}")

    def clear_role_assignments(self):
        self.role_assignments.clear()
        self.save_local()
        self.update_device_tree()
        self.status.config(text="Rol atamaları temizlendi. Program otomatik tanı kullanacak.")

    def request_selected_cfg(self):
        if not self.device_tree:
            return
        sel = self.device_tree.selection()
        if not sel:
            messagebox.showwarning("Seçim yok", "Önce listeden bir COM cihaz seç.")
            return
        d = self.devices.get(sel[0])
        if d:
            d.send("HELLO")
            d.send("GETCFG")
            self.status.config(text=f"{d.port} kimlik ve ayar bilgisi istendi.")

    def update_device_tree(self):
        if not self.device_tree:
            return
        existing = set(self.device_tree.get_children())
        for port, d in self.devices.items():
            detected = ROLE_LABELS.get(self.detected_role_code(d), "Bilinmeyen")
            assigned = ROLE_LABELS.get(self.role_assignments.get(port, ROLE_AUTO), ROLE_LABELS[ROLE_AUTO])
            effective = ROLE_LABELS.get(self.effective_role_code(d), "Bilinmeyen")
            short_status = d.last_status[:120] if d.last_status else "Kimlik bekleniyor"
            values = (port, detected, assigned, effective, short_status)
            if port in existing:
                self.device_tree.item(port, values=values)
                existing.remove(port)
            else:
                self.device_tree.insert("", "end", iid=port, values=values)
        for port in existing:
            self.device_tree.delete(port)


    def send_to(self, target, line):
        d = self.get_dev(target)
        if not d:
            messagebox.showwarning("Yok", f"{target} bulunamadı. Cihazları Tara yap.")
            return
        d.send(line)
        if line == "SAVE":
            self.log_current_settings(f"{target} hafızaya kaydedildi")

    def write_controller(self):
        d = self.get_dev("CONTROLLER")
        if not d:
            messagebox.showwarning("Controller yok", "Controller bulunamadı.")
            return
        # Röle yönü ayrı gösterilmez. Kredi Tercihi nasıl seçildiyse röle de aynı yöne ayarlanır.
        # Röle çalışma şekli V002M'de sabittir: kredi+start hazır, tetik basılıyken çeker.
        kredi_tercihi = option_value("COIN_AH", self.ctrl_vars["COIN_AH"].get())
        if "REL_AH" in self.ctrl_vars:
            self.ctrl_vars["REL_AH"].set(kredi_tercihi)
        if "RELAY_MODE" in self.ctrl_vars:
            self.ctrl_vars["RELAY_MODE"].set("0")
        for k, var in self.ctrl_vars.items():
            d.send(f"SET,{k},{option_value(k, var.get())}")
            time.sleep(0.02)
        for k, var in self.key_vars.items():
            d.send(f"SET,{k},{HID_KEYS.get(var.get(),0)}")
            time.sleep(0.02)
        d.send("SAVE")
        time.sleep(0.08)
        self.save_local("Kontrol kartı ayarları yazıldı")
        messagebox.showinfo("Tamam", "Kontrol kartı ayarları cihaza yazıldı, hafızaya kaydedildi ve tarih-saat ile kayıt altına alındı.")

    def write_mouse_settings(self, player):
        d = self.get_dev(player)
        if not d:
            messagebox.showwarning("Yok", f"{player} bulunamadı.")
            return
        for k,var in self.mouse_vars[player].items():
            d.send(f"SET,{k},{var.get()}")
            time.sleep(0.02)
        d.send("SAVE")
        time.sleep(0.08)
        self.save_local(f"{player} mouse ayarları yazıldı")
        messagebox.showinfo("Tamam", f"{player} mouse ayarları cihaza yazıldı, hafızaya kaydedildi ve tarih-saat ile kayıt altına alındı.")

    def set_jitter_level(self, player, filt, thr, name):
        self.mouse_vars[player]["FILTER"].set(str(filt))
        self.mouse_vars[player]["THR"].set(str(thr))
        self.status.config(text=f"{player} titreşim azaltma: {name} seçildi. Cihaza yazmak için Mouse Ayarlarını Yaz ve Kaydet bas.")

    def log_current_settings(self, action):
        try:
            ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            lines = []
            lines.append("=" * 70)
            lines.append(f"Tarih Saat : {ts}")
            lines.append(f"İşlem      : {action}")
            lines.append("Kontrol Kartı:")
            for k, v in self.ctrl_vars.items():
                raw = option_value(k, v.get())
                extra = f"  [cihaza giden değer: {raw}]" if k in CONTROL_OPTIONS else ""
                if k == "REL_AH":
                    lines.append(f"  Röle tercihi otomatik (REL_AH) = Kredi Tercihi ile aynı, değer {raw}")
                else:
                    lines.append(f"  {CTRL_LABELS.get(k,k)} ({k}) = {v.get()}{extra}")
            lines.append("Klavye Tuşları:")
            for k, v in self.key_vars.items():
                lines.append(f"  {CTRL_LABELS.get(k,k)} ({k}) = {v.get()}")
            for p in ("P1", "P2"):
                lines.append(f"{p} Kalibrasyon / Mouse:")
                for k, v in self.mouse_vars[p].items():
                    lines.append(f"  {MOUSE_LABELS.get(k,k)} ({k}) = {v.get()}")
            LOG_FILE.open("a", encoding="utf-8").write("\n".join(lines) + "\n")
        except Exception:
            pass

    def handle_calibration_mouse_click(self, event=None):
        # Kalibrasyon aktifken ayrı "Mouse ile Onayla" düğmesi yoktur.
        # Ekrana yapılan normal mouse tıklaması sıradaki köşeyi onaylar.
        # Kalibrasyona Başlat düğmesine basılan aynı tıklamayı almamak için kısa süre beklenir.
        now_ms = int(time.time() * 1000)
        try:
            widget_text = event.widget.cget("text")
        except Exception:
            widget_text = ""
        if widget_text in ("Kalibrasyona Başlat", "Kalibrasyonu İptal Et"):
            return
        current_tab = self.tabs.tab(self.tabs.select(), "text") if hasattr(self, "tabs") else ""
        if "Oyuncu 1" in current_tab and self.cal_active.get("P1", False):
            if now_ms - self.cal_start_ms.get("P1", 0) > 500:
                self.confirm_calibration_step("P1", "Mouse")
        elif "Oyuncu 2" in current_tab and self.cal_active.get("P2", False):
            if now_ms - self.cal_start_ms.get("P2", 0) > 500:
                self.confirm_calibration_step("P2", "Mouse")

    def start_calibration_flow(self, player):
        self.capture[player].clear()
        self.cal_active[player] = True
        self.cal_index[player] = 0
        code, name = CAL_STEPS[0]
        self.cal_step_vars[player].set(f"{name}: silahı/mouse'u sol üst köşeye götür, mouse tıkla veya tetik bas")
        log = getattr(self, f"{player}_log", None)
        if log:
            log.insert("end", "\n=== Yeni sıralı kalibrasyon başladı ===\n")
            log.insert("end", f"Sıradaki: {name}\n")
            log.see("end")
        self.status.config(text=f"{player} kalibrasyon başladı. Önce {name} onaylanacak.")

    def cancel_calibration_flow(self, player):
        self.cal_active[player] = False
        self.cal_index[player] = 0
        self.cal_step_vars[player].set("Kalibrasyon iptal edildi")
        self.status.config(text=f"{player} kalibrasyon iptal edildi.")

    def confirm_calibration_step(self, player, method="Mouse"):
        if not self.cal_active.get(player, False):
            messagebox.showwarning("Başlatılmadı", "Önce Kalibrasyona Başlat düğmesine bas.")
            return
        if self.cal_index[player] >= len(CAL_STEPS):
            return
        code, name = CAL_STEPS[self.cal_index[player]]
        if not self.capture_corner(player, code, method=method, show_warning=True):
            return
        self.cal_index[player] += 1
        log = getattr(self, f"{player}_log", None)
        if self.cal_index[player] >= len(CAL_STEPS):
            self.cal_active[player] = False
            self.cal_step_vars[player].set("4 köşe tamamlandı. Kalibrasyon yazılıyor ve hafızaya kaydediliyor...")
            if log:
                log.insert("end", "4 köşe tamamlandı. Cihaza yazılıyor ve hafızaya kaydediliyor...\n")
                log.see("end")
            self.write_mouse_cal(player)
            self.cal_step_vars[player].set("Kalibrasyon tamamlandı ve hafızaya kaydedildi")
            return
        next_code, next_name = CAL_STEPS[self.cal_index[player]]
        self.cal_step_vars[player].set(f"{next_name}: şimdi bu köşeye götür, mouse tıkla veya tetik bas")
        if log:
            log.insert("end", f"Sıradaki: {next_name}\n")
            log.see("end")
        self.status.config(text=f"{player}: {name} alındı. Sıradaki {next_name}.")

    def arm_trigger_capture(self, player, code):
        # Eski fonksiyon adı uyumluluk için duruyor. Yeni akışta tek tek köşe seçimi yok.
        self.start_calibration_flow(player)

    def capture_corner(self, player, code, method="Mouse", show_warning=True):
        d = self.get_dev(player)
        if not d or not d.last_status:
            if show_warning:
                messagebox.showwarning("Veri yok", f"{player} RAW değeri yok. Cihazları Tara yap ve silah Pico'nun bağlı olduğundan emin ol.")
            return False
        parts = d.last_status.split(",")
        try:
            i = parts.index("RAW")
            x, y = int(parts[i+1]), int(parts[i+2])
        except Exception:
            if show_warning:
                messagebox.showwarning("Okunamadı", d.last_status)
            return False
        self.capture[player][code] = (x,y)
        step_name = dict(CAL_STEPS).get(code, code)
        log = getattr(self, f"{player}_log", None)
        if log:
            log.insert("end", f"{step_name} [{method}] = RAW {x},{y}\n")
            log.see("end")
        return True

    def write_mouse_cal(self, player):
        cap = self.capture[player]
        if not all(k in cap for k in ["LU", "RU", "RD", "LD"]):
            messagebox.showwarning("Eksik", "4 köşeyi de sırayla onayla.")
            return

        # Her RAW sütununun yatay ve dikey harekete ne kadar cevap verdiğini ölç.
        # Böylece GP26/GP27 fiziksel olarak çapraz bağlanmış olsa da X ve Y otomatik bulunur.
        lu, ru, rd, ld = cap["LU"], cap["RU"], cap["RD"], cap["LD"]
        h0 = ((ru[0] + rd[0]) - (lu[0] + ld[0])) / 2.0
        v0 = ((rd[0] + ld[0]) - (lu[0] + ru[0])) / 2.0
        h1 = ((ru[1] + rd[1]) - (lu[1] + ld[1])) / 2.0
        v1 = ((rd[1] + ld[1]) - (lu[1] + ru[1])) / 2.0

        normal_score = abs(h0) + abs(v1)  # RAW-0 = X, RAW-1 = Y
        swap_score = abs(h1) + abs(v0)    # RAW-1 = X, RAW-0 = Y
        swap_axes = swap_score > normal_score
        x_index, y_index = (1, 0) if swap_axes else (0, 1)

        d = self.get_dev(player)
        current_x_adc, current_y_adc = 0, 1
        if d and d.last_status:
            current_x_adc = self._status_value(d.last_status, "XADC", 0)
            current_y_adc = self._status_value(d.last_status, "YADC", 1)
        current_channels = [current_x_adc, current_y_adc]
        new_x_adc = current_channels[x_index]
        new_y_adc = current_channels[y_index]

        # Güvenlik: iki eksen hiçbir zaman aynı ADC kanalına atanmasın.
        if new_x_adc == new_y_adc or new_x_adc not in (0, 1) or new_y_adc not in (0, 1):
            new_x_adc, new_y_adc = (1, 0) if swap_axes else (0, 1)

        x_values = [cap[k][x_index] for k in ["LU", "RU", "RD", "LD"]]
        y_values = [cap[k][y_index] for k in ["LU", "RU", "RD", "LD"]]
        xmin, xmax = min(x_values), max(x_values)
        ymin, ymax = min(y_values), max(y_values)

        horizontal_delta = (h1 if x_index == 1 else h0)
        vertical_delta = (v1 if y_index == 1 else v0)
        invx = 1 if horizontal_delta < 0 else 0
        invy = 1 if vertical_delta < 0 else 0

        if (xmax - xmin) < 50 or (ymax - ymin) < 50:
            messagebox.showwarning(
                "Kalibrasyon hareketi yetersiz",
                "X veya Y hareket aralığı çok küçük okundu. 4 köşeyi daha belirgin gösterip tekrar kalibre et.",
            )
            return

        self.mouse_vars[player]["XMIN"].set(str(xmin))
        self.mouse_vars[player]["XMAX"].set(str(xmax))
        self.mouse_vars[player]["YMIN"].set(str(ymin))
        self.mouse_vars[player]["YMAX"].set(str(ymax))
        self.mouse_vars[player]["INVX"].set(str(invx))
        self.mouse_vars[player]["INVY"].set(str(invy))

        log = getattr(self, f"{player}_log", None)
        if log:
            durum = "X/Y değiştirildi" if swap_axes else "X/Y sırası doğru"
            log.insert(
                "end",
                f"Otomatik eksen sonucu: {durum}; X=ADC{new_x_adc} Y=ADC{new_y_adc}; "
                f"X ters={invx}, Y ters={invy}\n",
            )
            log.see("end")

        if d:
            # Kanal, sınır ve yönleri tek komutta yazar; Pico flash hafızasına da kaydeder.
            d.send(
                f"CALXY,{new_x_adc},{new_y_adc},{xmin},{xmax},{ymin},{ymax},{invx},{invy}"
            )
            time.sleep(0.15)
            d.send("GETCFG")

        self.save_local(f"{player} otomatik X/Y kalibrasyonu yazıldı")
        sonuc = "X/Y otomatik değiştirildi" if swap_axes else "X/Y otomatik doğrulandı"
        messagebox.showinfo(
            "Tamam",
            f"{player} 4 köşe kalibrasyonu tamamlandı.\n"
            f"{sonuc}. X=ADC{new_x_adc}, Y=ADC{new_y_adc}.\n"
            f"Yönler ve sınırlar Pico hafızasına kaydedildi.",
        )

    def _status_value(self, line, key, default=0):
        try:
            parts = line.split(",")
            i = parts.index(key)
            return int(parts[i+1])
        except Exception:
            return default

    def check_trigger_calibration(self):
        ctrl = self.get_dev("CONTROLLER")
        if not ctrl or not ctrl.last_status:
            return
        t1 = self._status_value(ctrl.last_status, "T1", 0)
        t2 = self._status_value(ctrl.last_status, "T2", 0)
        current_tab = self.tabs.tab(self.tabs.select(), "text") if hasattr(self, "tabs") else ""
        if t1 and not self._last_t1:
            if self.cal_active.get("P1", False):
                self.confirm_calibration_step("P1", "Tetik")
            elif "Oyuncu 1" in current_tab:
                self.start_calibration_flow("P1")
        if t2 and not self._last_t2:
            if self.cal_active.get("P2", False):
                self.confirm_calibration_step("P2", "Tetik")
            elif "Oyuncu 2" in current_tab:
                self.start_calibration_flow("P2")
        self._last_t1, self._last_t2 = t1, t2

    def refresh(self):
        self.check_trigger_calibration()
        lines = []
        for port, d in self.devices.items():
            lines.append(f"{port}  KIND={d.kind}  PLAYER={d.player}")
            if d.last_status: lines.append("  " + d.last_status)
            if d.cfg: lines.append("  CFG=" + json.dumps(d.cfg, ensure_ascii=False))
            while not d.q.empty():
                line = d.q.get_nowait()
                if hasattr(self, "test_text"):
                    self.test_text.insert("end", f"{port}: {line}\n"); self.test_text.see("end")
        if hasattr(self, "dev_text"):
            self.dev_text.delete("1.0", "end")
            self.dev_text.insert("1.0", "\n".join(lines) if lines else "Cihaz yok. Cihazları Tara / Yenile yap.")
        self.after(500, self.refresh)

    def on_close(self):
        self.save_local()
        for d in self.devices.values(): d.close()
        self.destroy()

if __name__ == "__main__":
    App().mainloop()
