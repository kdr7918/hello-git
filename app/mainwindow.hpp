#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>

class CalibreTextDockTable;

// 메인 윈도우는 메뉴와 Dock 배치만 담당하고 RDB 로딩은 Dock에 위임한다.
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = 0);

private:
    void ChooseAndLoadRdbIndex(bool allParameters);

    CalibreTextDockTable* calibre_text_dock_;
};

#endif // MAINWINDOW_HPP
