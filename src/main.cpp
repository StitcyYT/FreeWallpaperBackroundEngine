#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>

#include <gtk/gtk.h>

#include "engine.h"
#include "ipc.h"

static Engine g_engine;
static Ipc g_ipc;
static bool g_killRequested = false;
static GtkWidget* g_window = nullptr;
static GtkWidget* g_statusLabel = nullptr;
static GtkWidget* g_playBtn = nullptr;
static GtkWidget* g_fileLabel = nullptr;
static GtkWidget* g_videoInfoLabel = nullptr;
static GtkWidget* g_speedLabel = nullptr;

static void showWindow() {
    if (g_window) {
        gtk_window_present(GTK_WINDOW(g_window));
    }
}

static gboolean onDelete(GtkWidget* w, GdkEvent*, gpointer) {
    std::fprintf(stderr, "WBE still running. Use 'desktop-wallpaper' to show window "
                         "or 'desktop-wallpaper --kill' to stop.\n");
    return gtk_widget_hide_on_delete(w);
}

static void onFileSet(GtkFileChooserButton* btn, gpointer) {
    char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(btn));
    if (path) {
        gtk_label_set_text(GTK_LABEL(g_fileLabel), path);
        g_engine.play(path);
        g_free(path);
    }
}

static void onPlay(GtkButton*, gpointer) {
    if (g_engine.isPlaying()) {
        g_engine.stop();
    } else {
        g_engine.resume();
    }
}

static void onKill(GtkButton*, gpointer) {
    g_engine.quit();
    gtk_main_quit();
}

static void onSpeedChanged(GtkRange* range, gpointer) {
    double val = gtk_range_get_value(range);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2fx", val);
    gtk_label_set_text(GTK_LABEL(g_speedLabel), buf);
    g_engine.setSpeed(val);
}

static gboolean updateUI(gpointer) {
    static int lastPlayState = -1;

    if (g_killRequested) {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    if (g_engine.hasError()) {
        gtk_label_set_text(GTK_LABEL(g_statusLabel), g_engine.error().c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(g_statusLabel), g_engine.status().c_str());
    }

    std::string info = g_engine.videoInfo();
    gtk_label_set_text(GTK_LABEL(g_videoInfoLabel), info.c_str());

    int playing = g_engine.isPlaying() ? 1 : 0;
    if (playing != lastPlayState) {
        lastPlayState = playing;
        if (g_engine.isActive() && playing) {
            gtk_button_set_label(GTK_BUTTON(g_playBtn), "⏸ Stop");
        } else if (g_engine.isActive()) {
            gtk_button_set_label(GTK_BUTTON(g_playBtn), "▶ Resume");
        } else {
            gtk_button_set_label(GTK_BUTTON(g_playBtn), "▶ Play");
        }
    }

    return G_SOURCE_CONTINUE;
}

extern "C" void signalHandler(int) {
    g_engine.quit();
    g_killRequested = true;
}

int main(int argc, char** argv) {
    if (g_ipc.tryConnect()) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--kill") == 0) {
                g_ipc.sendKill();
                return 0;
            }
        }
        g_ipc.sendShow();
        return 0;
    }

    Ipc* ipc = &g_ipc;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    gtk_init(&argc, &argv);

    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_window), "WBE Control");
    gtk_window_set_default_size(GTK_WINDOW(g_window), 440, 260);
    gtk_window_set_position(GTK_WINDOW(g_window), GTK_WIN_POS_CENTER);
    g_signal_connect(g_window, "delete-event", G_CALLBACK(onDelete), nullptr);
    g_signal_connect(g_window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget* fileBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* fileBtn = gtk_file_chooser_button_new("Select Video", GTK_FILE_CHOOSER_ACTION_OPEN);

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Video files");
    gtk_file_filter_add_mime_type(filter, "video/*");
    gtk_file_filter_add_mime_type(filter, "image/gif");
    const char* patterns[] = {"*.mp4", "*.avi", "*.mkv", "*.mov", "*.webm", "*.flv", "*.wmv", "*.gif", nullptr};
    for (const char** p = patterns; *p; p++)
        gtk_file_filter_add_pattern(filter, *p);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileBtn), filter);

    GtkFileFilter* allFilter = gtk_file_filter_new();
    gtk_file_filter_set_name(allFilter, "All files");
    gtk_file_filter_add_pattern(allFilter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fileBtn), allFilter);

    gtk_box_pack_start(GTK_BOX(fileBox), fileBtn, FALSE, FALSE, 0);
    g_fileLabel = gtk_label_new("No file selected");
    gtk_box_pack_start(GTK_BOX(fileBox), g_fileLabel, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), fileBox, FALSE, FALSE, 0);

    GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    g_playBtn = gtk_button_new_with_label("▶ Play");
    gtk_box_pack_start(GTK_BOX(btnBox), g_playBtn, FALSE, FALSE, 0);

    GtkWidget* killBtn = gtk_button_new_with_label("✕ Kill");
    gtk_box_pack_start(GTK_BOX(btnBox), killBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btnBox, FALSE, FALSE, 0);

    GtkWidget* speedBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* speedScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.25, 3.0, 0.05);
    gtk_range_set_value(GTK_RANGE(speedScale), 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(speedScale), FALSE);
    gtk_widget_set_size_request(speedScale, 180, -1);
    gtk_box_pack_start(GTK_BOX(speedBox), gtk_label_new("Speed:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(speedBox), speedScale, TRUE, TRUE, 0);
    g_speedLabel = gtk_label_new("1.00x");
    gtk_box_pack_start(GTK_BOX(speedBox), g_speedLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), speedBox, FALSE, FALSE, 0);

    g_videoInfoLabel = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox), g_videoInfoLabel, FALSE, FALSE, 0);

    g_statusLabel = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(vbox), g_statusLabel, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(g_window), vbox);

    g_signal_connect(fileBtn, "file-set", G_CALLBACK(onFileSet), nullptr);
    g_signal_connect(g_playBtn, "clicked", G_CALLBACK(onPlay), nullptr);
    g_signal_connect(killBtn, "clicked", G_CALLBACK(onKill), nullptr);
    g_signal_connect(speedScale, "value-changed", G_CALLBACK(onSpeedChanged), nullptr);

    if (!ipc->listen(showWindow)) {
        std::fprintf(stderr, "Warning: IPC listen failed\n");
    }

    gtk_widget_show_all(g_window);

    g_timeout_add(200, updateUI, nullptr);

    if (argc > 1) {
        GtkFileChooser* chooser = GTK_FILE_CHOOSER(fileBtn);
        gtk_file_chooser_set_filename(chooser, argv[1]);
        gtk_label_set_text(GTK_LABEL(g_fileLabel), argv[1]);
        g_engine.play(argv[1]);
    }

    gtk_main();

    g_engine.quit();
    return 0;
}
