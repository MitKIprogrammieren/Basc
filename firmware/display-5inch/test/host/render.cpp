// Rendert die echte Oberfläche (ui.cpp) auf dem PC in Bilddateien.
// Aufruf: render <todo_filament.json> <ausgabeordner>
// Erzeugt PPM-Dateien (800×480), siehe build.sh.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lvgl.h"
#include "../../src/model.h"
#include "../../src/ui.h"

static const int W = 800, H = 480;
static std::vector<lv_color_t> fb(W * H);

static void flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * W + x] = *px++;
    lv_disp_flush_ready(drv);
}

static void speichern(const std::string& pfad) {
    FILE* f = fopen(pfad.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (auto c : fb) {
        uint16_t v = c.full;
        unsigned char rgb[3] = {(unsigned char)(((v >> 11) & 0x1f) * 255 / 31),
                                (unsigned char)(((v >> 5) & 0x3f) * 255 / 63),
                                (unsigned char)((v & 0x1f) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void laufen(int ms) {
    for (int t = 0; t < ms; t += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

struct Protokoll : UiAktionen {
    void tara() override { puts("Aktion: tara"); }
    void kalibrieren(int g) override { printf("Aktion: kalibrieren %d\n", g); }
    void rest_speichern(const std::string& u, int n, const std::string& t) override {
        printf("Aktion: rest_speichern %s %d %s\n", u.c_str(), n, t.c_str());
    }
    void tag_zuordnen(const std::string& u, const std::string& t) override {
        printf("Aktion: tag_zuordnen %s %s\n", u.c_str(), t.c_str());
    }
    void spulen_aendern(const std::string& u, int d) override { printf("Aktion: spulen %s %d\n", u.c_str(), d); }
    void neu_laden() override { puts("Aktion: neu_laden"); }
};

int main(int argc, char** argv) {
    if (argc < 3) { puts("render <json> <ordner>"); return 1; }
    std::ifstream in(argv[1]);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string json = ss.str();
    Lager lager;
    if (!lager.lade_service_antwort(json.c_str(), json.size(), "todo.filament")) { puts("JSON-Fehler"); return 1; }
    printf("%zu Sorten geladen\n", lager.sorten.size());
    std::string aus = argv[2];

    lv_init();
    static lv_disp_draw_buf_t buf;
    static std::vector<lv_color_t> b1(W * 60);
    lv_disp_draw_buf_init(&buf, b1.data(), nullptr, W * 60);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = W;
    drv.ver_res = H;
    drv.flush_cb = flush;
    drv.draw_buf = &buf;
    lv_disp_drv_register(&drv);

    Protokoll p;
    ui_start(&p, &lager);

    WaagenStatus st;
    st.sensor_ok = st.wlan_ok = st.ha_ok = true;
    st.uhrzeit = "21:27";
    st.ip = "192.168.178.57";
    st.version = "1.0.0";
    st.leer_standard_g = 250;

    // 1: bekannte Spule per NFC rechts
    st.brutto_g = 812;
    st.stabil = true;
    st.tag_r = "04C0FFEE112233";
    ui_status(st);
    laufen(100);
    speichern(aus + "/1_wiegen.ppm");

    // 2: unbekannter Tag
    st.tag_r = "04DEADBEEF0001";
    st.brutto_g = 655;
    ui_status(st);
    laufen(100);
    speichern(aus + "/2_neuer_tag.ppm");

    // 3: leerer Teller
    st.tag_r.clear();
    st.brutto_g = 0.4f;
    ui_status(st);
    laufen(100);
    speichern(aus + "/3_leer.ppm");

    // 4: Lager
    ui_zeige("lager");
    laufen(100);
    speichern(aus + "/4_lager.ppm");

    // 5: Detail (Ash Gray)
    ui_zeige("detail:0");
    laufen(100);
    speichern(aus + "/5_detail.ppm");

    // 6: Einstellungen mit Meldung
    ui_zeige("einst");
    ui_status(st);
    ui_meldung("Rest 560 g → 562 g gespeichert");
    laufen(100);
    speichern(aus + "/6_einst.ppm");

    // 7: Speichern-Fehler auf Wiegen
    st.tag_r = "04C0FFEE112233";
    st.brutto_g = 812;
    ui_zeige("wiegen");
    ui_status(st);
    ui_meldung("Home Assistant nicht erreichbar", true);
    laufen(100);
    speichern(aus + "/7_fehler.ppm");
    puts("fertig");
    return 0;
}
