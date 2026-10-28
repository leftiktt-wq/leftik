#include "tweaks.hpp"
#include "resource.h"

// DPI: вся геометрия UI задана в логических пикселях (96 DPI).
// Scale() переводит логические единицы в физические на границах Win32
// (окно, hit-test, back-buffer). Внутри DrawWhole масштаб применяется
// одним Graphics::ScaleTransform — тогда масштабируются и все литералы
// координат, и шрифты, и толщины линий, без риска пропустить число.
static int g_dpi = 96;
static int g_bufW = 0, g_bufH = 0;

inline int Scale(int px) { return (int)(px * (g_dpi / 96.0f)); }
inline int Unscale(int px) { return g_dpi > 0 ? (int)(px * (96.0f / g_dpi)) : px; }

// Многострочный текст, ограниченный maxLines. Последняя строка обрезается многоточием.
void DrawTextWrapped(Graphics& g, const std::wstring& text, Font* font, RectF rect, Color color,
                     int maxLines = 3, StringAlignment align = StringAlignmentNear) {
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisWord);
    format.SetFormatFlags(StringFormatFlagsLineLimit);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, font, rect, &format, &brush);
    (void)maxLines;
}

void DrawProgressBar(Graphics& g, int x, int y, int w, int h, double frac, Color color) {
    frac = std::clamp(frac, 0.0, 1.0);
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h), h / 2.0f,
                  Color(255, 43, 43, 54), Color(0, 0, 0, 0));
    if (frac > 0.005)
        DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)(w * frac), (REAL)h), h / 2.0f,
                      color, Color(0, 0, 0, 0));
}

void DrawToggleSwitch(Graphics& g, int x, int y, int w, int h, bool on, Color onColor) {
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h), h / 2.0f,
                  on ? onColor : Color(255, 44, 44, 56),
                  on ? onColor : Color(255, 58, 58, 72));
    int knob = h - 6;
    int kx = on ? x + w - knob - 3 : x + 3;
    DrawRoundRect(g, RectF((REAL)kx, (REAL)(y + 3), (REAL)knob, (REAL)knob),
                  knob / 2.0f, on ? Color(255, 255, 255, 255) : Color(255, 190, 190, 202),
                  Color(0, 0, 0, 0));
}

void DrawIconTile(Graphics& g, int x, int y, int size, const wchar_t* glyph, Color bg, Color fg, Font* iconFont = nullptr) {
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)size, (REAL)size), 10.0f, bg, Color(0, 0, 0, 0));
    DrawTextGdi(g, glyph, iconFont ? iconFont : App::fIcon,
                RectF((REAL)x, (REAL)y, (REAL)size, (REAL)size), fg,
                StringAlignmentCenter, StringAlignmentCenter);
}



void SelectFirstTweakInCategory(App::Category cat) {
    for (size_t i = 0; i < App::tweaksList.size(); ++i) {
        if (App::tweaksList[i].cat == cat) {
            App::selectedTweakIdx = (int)i;
            return;
        }
    }
    App::selectedTweakIdx = -1;
}

constexpr int kTweakTop = 28;
constexpr int kTweakBottom = 28;
constexpr int kTweakItemH = 72;
constexpr int kTweakGap = 10;

int TweaksMaxScroll() {
    int total = 0;
    for (auto& t : App::tweaksList) {
        if (t.cat != App::activeCategory) continue;
        if (t.isPlacebo && !App::showPlaceboTweaks) continue;
        total++;
    }
    int visible = App::windowH - kTweakTop - kTweakBottom;
    int content = total * (kTweakItemH + kTweakGap) - kTweakGap;
    return std::min(0, visible - content);
}

void DrawCircularProgress(Graphics& g, int x, int y, int size, int percent, const std::wstring& title, const std::wstring& valueText) {
    SolidBrush bgBrush(Color(255, 20, 20, 25));
    g.FillEllipse(&bgBrush, x, y, size, size);
    
    Pen progressPen(App::ACCENT, 10.0f);
    progressPen.SetStartCap(LineCapRound);
    progressPen.SetEndCap(LineCapRound);
    
    g.DrawArc(&progressPen, (REAL)(x + 5), (REAL)(y + 5), (REAL)(size - 10), (REAL)(size - 10), -90.0f, (REAL)(percent * 3.6f));
    
    wchar_t buf[32]; swprintf(buf, 32, L"%d%%", percent);
    DrawTextGdi(g, buf, App::fTitle, RectF((REAL)x, (REAL)(y + size / 2 - 15), (REAL)size, 30), App::TEXT, StringAlignmentCenter);
}

void AddHit(int x, int y, int w, int h, int id) { App::hits.push_back({RECT{x, y, x + w, y + h}, id}); }

void DrawTweaks(Graphics& g) {
    const wchar_t* catNames[] = {L"Базовое", L"Безопасность", L"Электропитание", L"Сеть", L"Debloat", L"Приватность", L"Службы", L"Исправления"};
    const wchar_t* catIcons[] = {L"\uE80A", L"\uE72E", L"\uE945", L"\uE774", L"\uE74D", L"\uE890", L"\uE713", L"\uE90F"};

    int left = App::SIDE_W + 24;
    int top = kTweakTop;
    int bottom = App::windowH - kTweakBottom;
    int colH = bottom - top;
    int w = App::windowW - left - 32;
    int gap = 20;
    int catW = std::clamp((int)(w * 0.18f), 200, 238);
    int descW = std::clamp((int)(w * 0.28f), 310, 370);
    int listW = w - catW - descW - gap * 2;
    if (listW < 280) listW = 280;

    int listX = left + catW + gap;
    int descX = listX + listW + gap;

    {
        static std::wstring s_cachedAcName;
        static bool s_cachedAc = false;
        static ULONGLONG s_lastAcCheck = 0;
        ULONGLONG now = GetTickCount64();
        if (now - s_lastAcCheck > 3000) {
            s_lastAcCheck = now;
            s_cachedAc = IsAntiCheatActive(&s_cachedAcName);
        }
        if (s_cachedAc) {
            int bannerY = std::max(2, top - 26);
            DrawRoundRect(g, RectF((REAL)listX, (REAL)bannerY, (REAL)(listW + 12), 24), 6.0f,
                          Color(60, 220, 80, 80), App::RED);
            DrawTextGdi(g, L"⚠️ " + s_cachedAcName + L" активен — твики 1002/1017/1097/1099/1104 требуют подтверждения",
                        App::fSmall, RectF((REAL)listX, (REAL)bannerY, (REAL)(listW + 12), 24),
                        App::RED, StringAlignmentCenter, StringAlignmentCenter);
        }
    }

    // v58.4: переключатель "Показать устаревшие"
    {
        int placeboCount = 0;
        for (auto& t : App::tweaksList) if (t.isPlacebo) placeboCount++;
        if (placeboCount > 0) {
            int cbX = listX + listW - 190;
            int cbY = top - 26;
            DrawRoundRect(g, RectF((REAL)cbX, (REAL)cbY, 16, 16), 4.0f,
                          App::showPlaceboTweaks ? App::UI_ACCENT : App::UI_SIDEBAR,
                          App::showPlaceboTweaks ? App::UI_ACCENT : App::UI_BORDER);
            if (App::showPlaceboTweaks)
                DrawTextGdi(g, L"\uE73E", App::fIcon, RectF((REAL)cbX, (REAL)cbY, 16, 16),
                            Color(255,255,255,255), StringAlignmentCenter, StringAlignmentCenter);
            wchar_t lbl[64];
            swprintf(lbl, 64, L"Показать устаревшие (%d)", placeboCount);
            DrawTextGdi(g, lbl, App::fSmall,
                        RectF((REAL)(cbX + 20), (REAL)cbY, 170, 16),
                        App::UI_MUTED);
            AddHit(cbX, cbY, 190, 16, 2100);
        }
    }

    // Держим выделение внутри активной категории: правая карточка всегда актуальна.
    if (App::selectedTweakIdx < 0 || App::selectedTweakIdx >= (int)App::tweaksList.size() ||
        App::tweaksList[App::selectedTweakIdx].cat != App::activeCategory) {
        SelectFirstTweakInCategory(App::activeCategory);
    }

    // ---------- Колонка 1: категории ----------
    DrawRoundRect(g, RectF((REAL)left, (REAL)top, (REAL)catW, (REAL)colH), 12.0f, App::UI_CARD, App::UI_BORDER);
    int itemX = left + 10, itemW = catW - 20, itemY = top + 12;
    for (int i = 0; i < App::CAT_COUNT; ++i) {
        bool active = (App::activeCategory == i);
        if (active)
            DrawRoundRect(g, RectF((REAL)itemX, (REAL)itemY, (REAL)itemW, 46), 9.0f, App::UI_ACCENT, App::UI_ACCENT);
        DrawTextGdi(g, catIcons[i], App::fIcon,
                    RectF((REAL)(itemX + 12), (REAL)itemY, 26, 46),
                    active ? Color(255, 255, 255, 255) : App::UI_MUTED,
                    StringAlignmentCenter, StringAlignmentCenter);
        DrawTextEllipsis(g, catNames[i], active ? App::fBodyBold : App::fBody,
                         RectF((REAL)(itemX + 46), (REAL)itemY, (REAL)(itemW - 54), 46),
                         active ? Color(255, 255, 255, 255) : Color(255, 190, 190, 202));
        AddHit(itemX, itemY, itemW, 46, 2000 + i);
        itemY += 50;
    }

    // ---------- Колонка 2: список твиков ----------
    int maxScroll = TweaksMaxScroll();
    if (App::tweaksScrollY > 0) App::tweaksScrollY = 0;
    if (App::tweaksScrollY < maxScroll) App::tweaksScrollY = maxScroll;

    int rowY = top + App::tweaksScrollY;
    int totalItems = 0;
    GraphicsState listState = g.Save();
    g.SetClip(RectF((REAL)listX, (REAL)top, (REAL)(listW + 12), (REAL)colH));

    for (size_t i = 0; i < App::tweaksList.size(); ++i) {
        auto& tw = App::tweaksList[i];
        if (tw.cat != App::activeCategory) continue;
        // v58.4: скрываем "устаревшие" (плацебо на Win10/11), если не включён режим "Все"
        if (tw.isPlacebo && !App::showPlaceboTweaks) continue;
        totalItems++;

        int rowTop = rowY;
        int rowBottom = rowY + kTweakItemH;
        rowY += kTweakItemH + kTweakGap;
        if (rowBottom < top || rowTop > bottom) continue;

        bool selected = (App::selectedTweakIdx == (int)i);
        DrawRoundRect(g, RectF((REAL)listX, (REAL)rowTop, (REAL)listW, (REAL)kTweakItemH), 12.0f,
                      selected ? App::UI_CARD_HOVER : App::UI_CARD,
                      selected ? Color(255, 83, 66, 156) : App::UI_BORDER);

        DrawIconTile(g, listX + 12, rowTop + 13, 46, catIcons[App::activeCategory],
                     Color(255, 33, 33, 42),
                     selected ? App::UI_ACCENT : Color(255, 172, 172, 186),
                     App::fIconBig);
        if (tw.isPlacebo) {
            // Жёлтая метка в углу иконки
            SolidBrush placeboBrush(App::YELLOW);
            g.FillEllipse(&placeboBrush, (REAL)(listX + 48), (REAL)(rowTop + 14), 8.0f, 8.0f);
        } else if (tw.isDangerous) {
            SolidBrush dangerBrush(App::RED);
            g.FillEllipse(&dangerBrush, (REAL)(listX + 48), (REAL)(rowTop + 14), 8.0f, 8.0f);
        }

        int tglW = 44, tglH = 24;
        int tglX = listX + listW - tglW - 12;
        int tglY = rowTop + (kTweakItemH - tglH) / 2;
        int textW = listW - 72 - 108;
        if (textW < 80) textW = 80;

        DrawTextEllipsis(g, tw.name, App::fBodyBold,
                         RectF((REAL)(listX + 72), (REAL)(rowTop + 13), (REAL)textW, 21), App::UI_TEXT);
        DrawTextEllipsis(g, tw.shortDesc, App::fSmall,
                         RectF((REAL)(listX + 72), (REAL)(rowTop + 37), (REAL)textW, 18), App::UI_MUTED);

        if (tw.requiresReboot && tw.isEnabled) {
            DrawTextGdi(g, L"\uE823", App::fIcon,
                        RectF((REAL)(tglX - 28), (REAL)(rowTop + 27), 18, 18), App::YELLOW,
                        StringAlignmentCenter, StringAlignmentCenter);
        }

        const wchar_t* rowBlockReason = GetTweakBlockReason((int)tw.id);
        bool rowBlocked = (rowBlockReason != nullptr && !tw.isEnabled && !IsTweakWarningOnly((int)tw.id));
        if (rowBlocked) {
            DrawRoundRect(g, RectF((REAL)tglX, (REAL)tglY, (REAL)tglW, (REAL)tglH), 12.0f,
                          Color(255, 44, 44, 56), App::UI_BORDER);
            DrawTextGdi(g, L"\uE72E", App::fIcon,
                        RectF((REAL)tglX, (REAL)tglY, (REAL)tglW, (REAL)tglH), App::RED,
                        StringAlignmentCenter, StringAlignmentCenter);
        } else if (tw.isLaptopSafe == false && App::isLaptop && !tw.isEnabled) {
            DrawRoundRect(g, RectF((REAL)tglX, (REAL)tglY, (REAL)tglW, (REAL)tglH), 12.0f,
                          Color(255, 44, 44, 56), App::UI_BORDER);
            DrawTextGdi(g, L"NB", App::fSmall,
                        RectF((REAL)tglX, (REAL)tglY, (REAL)tglW, (REAL)tglH), App::RED,
                        StringAlignmentCenter, StringAlignmentCenter);
        } else {
            DrawToggleSwitch(g, tglX, tglY, tglW, tglH, tw.isEnabled, App::UI_GREEN);
        }

        int hitTop = std::max(rowTop, top);
        int hitBottom = std::min(rowBottom, bottom);
        if (hitBottom > hitTop)
            AddHit(listX, hitTop, listW, hitBottom - hitTop, 5000 + (int)i);
        if (rowTop >= top && rowBottom <= bottom)
            AddHit(tglX - 6, tglY - 6, tglW + 12, tglH + 12, 6000 + (int)i);
    }
    g.Restore(listState);

    if (totalItems > 0) {
        int contentH = totalItems * (kTweakItemH + kTweakGap) - kTweakGap;
        if (contentH > colH) {
            int scrollX = listX + listW + 6;
            DrawRoundRect(g, RectF((REAL)scrollX, (REAL)top, 4, (REAL)colH), 2.0f,
                          Color(255, 35, 35, 44), Color(0, 0, 0, 0));
            float thumbH = std::max(28.0f, (float)colH * (float)colH / (float)contentH);
            float denom = (float)(contentH - colH);
            float pos = denom > 0.0f ? (-(float)App::tweaksScrollY / denom) : 0.0f;
            pos = std::clamp(pos, 0.0f, 1.0f);
            float thumbY = top + pos * (colH - thumbH);
            DrawRoundRect(g, RectF((REAL)scrollX, (REAL)thumbY, 4, (REAL)thumbH), 2.0f,
                          App::UI_ACCENT, Color(0, 0, 0, 0));
        }
    }

    // ---------- Колонка 3: описание ----------
    DrawRoundRect(g, RectF((REAL)descX, (REAL)top, (REAL)descW, (REAL)colH), 12.0f,
                  App::UI_CARD, App::UI_BORDER);

    if (App::selectedTweakIdx >= 0 && App::selectedTweakIdx < (int)App::tweaksList.size()) {
        auto& tw = App::tweaksList[App::selectedTweakIdx];
        int pad = 20;
        int innerW = descW - pad * 2;

        DrawIconTile(g, descX + pad, top + pad, 44, catIcons[tw.cat],
                     App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
        DrawTextEllipsis(g, tw.name, App::fH2,
                         RectF((REAL)(descX + pad + 56), (REAL)(top + pad + 2), (REAL)(innerW - 56), 25), App::UI_TEXT);
        DrawTextEllipsis(g, tw.shortDesc, App::fSmall,
                         RectF((REAL)(descX + pad + 56), (REAL)(top + pad + 29), (REAL)(innerW - 56), 18), App::UI_MUTED);

        int badgeY = top + 78;
        if (tw.isEnabled) {
            DrawRoundRect(g, RectF((REAL)(descX + pad), (REAL)badgeY, 104, 25), 12.5f,
                          App::UI_GREEN_DIM, App::UI_GREEN);
            DrawTextGdi(g, L"Включено", App::fSmall,
                        RectF((REAL)(descX + pad), (REAL)badgeY, 104, 25), App::UI_GREEN,
                        StringAlignmentCenter, StringAlignmentCenter);
        } else {
            DrawRoundRect(g, RectF((REAL)(descX + pad), (REAL)badgeY, 104, 25), 12.5f,
                          Color(255, 40, 40, 50), App::UI_BORDER);
            DrawTextGdi(g, L"Выключено", App::fSmall,
                        RectF((REAL)(descX + pad), (REAL)badgeY, 104, 25), App::UI_MUTED,
                        StringAlignmentCenter, StringAlignmentCenter);
        }
        if (tw.isDangerous) {
            DrawRoundRect(g, RectF((REAL)(descX + pad + 112), (REAL)badgeY, 84, 25), 12.5f,
                          Color(255, 72, 29, 35), Color(255, 120, 52, 62));
            DrawTextGdi(g, L"Опасно", App::fSmall,
                        RectF((REAL)(descX + pad + 112), (REAL)badgeY, 84, 25), App::RED,
                        StringAlignmentCenter, StringAlignmentCenter);
        }

        int bodyY = top + 118;
        int bodyH = 100;
        DrawTextWrapped(g, tw.fullDesc, App::fBody,
                        RectF((REAL)(descX + pad), (REAL)bodyY, (REAL)innerW, (REAL)bodyH),
                        App::UI_BODY_TEXT, 5);

        int advY = bodyY + bodyH + 6;
        DrawTextGdi(g, L"Преимущества", App::fBold,
                    RectF((REAL)(descX + pad), (REAL)advY, (REAL)innerW, 24), App::UI_TEXT);

        const wchar_t* adv[3];
        switch (tw.cat) {
            case App::CAT_BASIC:      adv[0]=L"Больше FPS в играх"; adv[1]=L"Меньше фоновой нагрузки"; adv[2]=L"Снижение потребления ресурсов"; break;
            case App::CAT_SECURITY:   adv[0]=L"Меньше лишних проверок"; adv[1]=L"Освобождает ресурсы"; adv[2]=L"Откат в один клик"; break;
            case App::CAT_POWER:      adv[0]=L"Стабильный фреймтайм"; adv[1]=L"Снижение троттлинга"; adv[2]=L"Экономия ресурсов"; break;
            case App::CAT_NETWORK:    adv[0]=L"Ниже пинг"; adv[1]=L"Стабильное соединение"; adv[2]=L"Меньше джиттера"; break;
            case App::CAT_DEBLOAT:    adv[0]=L"Освобождает ОЗУ"; adv[1]=L"Меньше фоновых процессов"; adv[2]=L"Ускоряет запуск"; break;
            case App::CAT_PRIVACY:    adv[0]=L"Меньше сбора данных"; adv[1]=L"Отключение телеметрии"; adv[2]=L"Полный откат"; break;
            case App::CAT_SERVICES:   adv[0]=L"Меньше фоновых служб"; adv[1]=L"Экономия ОЗУ"; adv[2]=L"Обратимо"; break;
            default:                  adv[0]=L"Восстанавливает работу"; adv[1]=L"Безопасные команды"; adv[2]=L"Журнал изменений"; break;
        }
        int advItemY = advY + 30;
        for (int k = 0; k < 3; ++k) {
            DrawRoundRect(g, RectF((REAL)(descX + pad), (REAL)(advItemY + 1), 18, 18), 9.0f,
                          App::UI_GREEN_DIM, Color(0, 0, 0, 0));
            DrawTextGdi(g, L"\uE73E", App::fIcon,
                        RectF((REAL)(descX + pad), (REAL)(advItemY + 1), 18, 18), App::UI_GREEN,
                        StringAlignmentCenter, StringAlignmentCenter);
            DrawTextEllipsis(g, adv[k], App::fSmall,
                             RectF((REAL)(descX + pad + 26), (REAL)(advItemY + 1), (REAL)(innerW - 26), 18),
                             Color(255, 205, 205, 216));
            advItemY += 30;
        }

        // Кнопка внизу правой карточки
        int btnY = bottom - 132;
        const wchar_t* blockReason = GetTweakBlockReason((int)tw.id);
        bool blocked = (blockReason != nullptr && !tw.isEnabled && !IsTweakWarningOnly((int)tw.id));
        if (blocked) {
            DrawRoundRect(g, RectF((REAL)(descX + pad), (REAL)btnY, (REAL)innerW, 46), 10.0f,
                          Color(255, 33, 33, 42), Color(255, 110, 48, 58));
            DrawTextGdi(g, L"Заблокировано Guard", App::fBodyBold,
                        RectF((REAL)(descX + pad), (REAL)btnY, (REAL)innerW, 46), App::RED,
                        StringAlignmentCenter, StringAlignmentCenter);
        } else if (tw.isEnabled) {
            DrawRoundRect(g, RectF((REAL)(descX + pad), (REAL)btnY, (REAL)innerW, 46), 10.0f,
                          Color(255, 35, 35, 44), App::UI_BORDER);
            DrawTextGdi(g, L"Отключить твик", App::fBodyBold,
                        RectF((REAL)(descX + pad), (REAL)btnY, (REAL)innerW, 46), App::UI_TEXT,
                        StringAlignmentCenter, StringAlignmentCenter);
        } else {
            DrawRoundRect(g, RectF((REAL)(descX + pad), (REAL)btnY, (REAL)innerW, 46), 10.0f,
                          App::UI_ACCENT, App::UI_ACCENT);
            DrawTextGdi(g, L"Включить твик", App::fBodyBold,
                        RectF((REAL)(descX + pad), (REAL)btnY, (REAL)innerW, 46),
                        Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
        }
        AddHit(descX + pad, btnY, innerW, 46, 6000 + App::selectedTweakIdx);

        // Нижняя информационная карточка «Раздел / Статус»
        int infoY = bottom - 72;
        DrawRoundRect(g, RectF((REAL)(descX + pad), (REAL)infoY, (REAL)innerW, 52), 10.0f,
                      Color(255, 22, 22, 28), App::UI_BORDER);
        int midX = descX + pad + innerW / 2;
        DrawTextGdi(g, L"Раздел", App::fSmall,
                    RectF((REAL)(descX + pad + 12), (REAL)(infoY + 8), 100, 16), App::UI_MUTED);
        DrawTextEllipsis(g, catNames[tw.cat], App::fBodyBold,
                         RectF((REAL)(descX + pad + 12), (REAL)(infoY + 26), (REAL)(innerW / 2 - 20), 20), App::UI_TEXT);
        DrawTextGdi(g, L"Статус", App::fSmall,
                    RectF((REAL)(midX + 12), (REAL)(infoY + 8), 100, 16), App::UI_MUTED);
        DrawTextEllipsis(g, tw.isEnabled ? L"Включено" : L"Выключено", App::fBodyBold,
                         RectF((REAL)(midX + 12), (REAL)(infoY + 26), (REAL)(innerW / 2 - 20), 20),
                         tw.isEnabled ? App::UI_GREEN : App::UI_MUTED);
    } else {
        DrawTextGdi(g, L"Выберите твик в списке", App::fBody,
                    RectF((REAL)descX, (REAL)(top + 30), (REAL)descW, 24), App::UI_MUTED,
                    StringAlignmentCenter, StringAlignmentCenter);
    }
}

void DrawSidebar(Graphics& g) {
    SolidBrush bgBrush(App::UI_SIDEBAR);
    g.FillRectangle(&bgBrush, 0, 0, App::SIDE_W, App::windowH);
    Pen edge(App::UI_BORDER, 1.0f);
    g.DrawLine(&edge, App::SIDE_W - 1, 0, App::SIDE_W - 1, App::windowH);

    // Логотип и название
    DrawRoundRect(g, RectF(24, 26, 44, 44), 12.0f, App::UI_ACCENT, App::UI_ACCENT);
    DrawTextGdi(g, L"\uE713", App::fIconBig, RectF(24, 26, 44, 44),
                Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
    DrawTextEllipsis(g, L"LEFTIK", App::fTitle, RectF(82, 25, 150, 30), App::UI_TEXT);
    DrawTextEllipsis(g, L"Оптимизатор с откатом", App::fSmall, RectF(82, 55, 158, 18), App::UI_MUTED);
    DrawRoundRect(g, RectF(82, 80, 108, 20), 10.0f, App::UI_GREEN_DIM, Color(0, 0, 0, 0));
    DrawTextGdi(g, L"FREE · 2026.1", App::fSmall, RectF(82, 80, 108, 20), App::UI_GREEN,
                StringAlignmentCenter, StringAlignmentCenter);

    struct Nav { const wchar_t* name; const wchar_t* icon; App::Page p; };
    Nav navs[] = {
        {L"Главная", L"\uE80F", App::Dashboard},
        {L"Оптимизация", L"\uE945", App::Tweaks},
        {L"Игры", L"\uE7FC", App::Games},
        {L"Очистка", L"\uE74D", App::Cleaner},
        {L"Восстановление", L"\uE81C", App::Restore},
        {L"Диагностика", L"\uE9D9", App::Diagnostics},
        {L"Бенчмарк", L"\uE9D2", App::BenchmarkPage},
        {L"Датчики", L"\uE9D9", App::Sensors}
    };

    int y = 128;
    for (auto& n : navs) {
        bool active = (App::page == n.p);
        if (active) {
            DrawRoundRect(g, RectF(12, (REAL)y, (REAL)(App::SIDE_W - 24), 46), 10.0f,
                          App::UI_ACCENT_DIM, Color(0, 0, 0, 0));
            DrawRoundRect(g, RectF(12, (REAL)(y + 11), 4, 24), 2.0f, App::UI_ACCENT,
                          Color(0, 0, 0, 0));
        }
        DrawTextGdi(g, n.icon, App::fIcon,
                    RectF(28, (REAL)y, 26, 46),
                    active ? App::UI_ACCENT : Color(255, 160, 160, 174),
                    StringAlignmentCenter, StringAlignmentCenter);
        DrawTextEllipsis(g, n.name, active ? App::fBodyBold : App::fNav,
                         RectF(58, (REAL)y, (REAL)(App::SIDE_W - 78), 46),
                         active ? App::UI_TEXT : Color(255, 186, 186, 198));
        AddHit(12, y, App::SIDE_W - 24, 46, (int)n.p + 100);
        y += 50;
    }

    // Подвал: ОС и версия
    int footY = App::windowH - 100;
    Pen footLine(App::UI_BORDER, 1.0f);
    g.DrawLine(&footLine, 24, footY, App::SIDE_W - 24, footY);
    {
        SolidBrush wb(Color(255, 160, 160, 174));
        int wx = 24, wy = App::windowH - 76;
        g.FillRectangle(&wb, wx, wy, 7, 7);
        g.FillRectangle(&wb, wx + 9, wy, 7, 7);
        g.FillRectangle(&wb, wx, wy + 9, 7, 7);
        g.FillRectangle(&wb, wx + 9, wy + 9, 7, 7);
    }
    std::wstring osText = L"Windows";
    if (App::hwInfo.winBuild >= 22000) osText = L"Windows 11";
    else if (App::hwInfo.winBuild > 0) osText = L"Windows 10";
    DrawTextEllipsis(g, osText, App::fSmall, RectF(50, App::windowH - 80, 180, 18), App::UI_TEXT);
    DrawTextEllipsis(g, L"Версия 2026.1 · Бесплатно", App::fSmall, RectF(50, App::windowH - 60, 190, 18), App::UI_MUTED);
}

void DrawCleaner(Graphics& g) {
    int x = App::SIDE_W + 30, y = 40, w = App::windowW - App::SIDE_W - 60;
    DrawTextGdi(g, L"ОЧИСТКА СИСТЕМЫ", App::fTitle, RectF((REAL)x, (REAL)y, (REAL)w, 40), App::UI_TEXT); y += 55;

    int leftW = w - 320;
    const wchar_t* items[] = {L"Временные файлы (Temp)", L"Кэш Windows (Prefetch)", L"Корзина", L"Логи и отчеты"};
    const wchar_t* descs[] = {L"Удаляет мусор из %temp%", L"Ускоряет запуск программ", L"Очищает удаленные файлы", L"Удаляет старые логи"};
    bool* states[] = {&App::cleanTemp, &App::cleanPrefetch, &App::cleanRecycle, &App::cleanLogs};
    
    for (int i = 0; i < 4; ++i) {
        DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)leftW, 80), 12.0f, App::UI_CARD, App::UI_BORDER);
        
        DrawRoundRect(g, RectF((REAL)(x + 18), (REAL)(y + 29), 22, 22), 6.0f, *states[i] ? App::UI_ACCENT : App::UI_SIDEBAR, *states[i] ? App::UI_ACCENT : App::UI_BORDER);
        if (*states[i]) {
            DrawTextGdi(g, L"\uE73E", App::fIcon, RectF((REAL)(x + 18), (REAL)(y + 29), 22, 22), Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
        }
        
        DrawTextGdi(g, items[i], App::fBold, RectF((REAL)(x + 56), (REAL)(y + 18), (REAL)(leftW - 80), 24), App::UI_TEXT);
        DrawTextGdi(g, descs[i], App::fSmall, RectF((REAL)(x + 56), (REAL)(y + 45), (REAL)(leftW - 80), 20), App::UI_MUTED);
        
        AddHit(x, y, leftW, 80, 3301 + i); 
        y += 92;
    }

    int btnY = App::windowH - 190;
    DrawRoundRect(g, RectF((REAL)x, (REAL)btnY, (REAL)leftW, 48), 12.0f, App::UI_ACCENT, App::UI_ACCENT);
    DrawTextGdi(g, L"ОЧИСТИТЬ ВЫБРАННОЕ", App::fBold, RectF((REAL)x, (REAL)btnY, (REAL)leftW, 48), Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
    AddHit(x, btnY, leftW, 48, 3200);

    // Ряд из двух кнопок: глубокая очистка и удаление пустых хвостов
    int halfW = (leftW - 12) / 2;
    int rowY = btnY + 58;
    DrawRoundRect(g, RectF((REAL)x, (REAL)rowY, (REAL)halfW, 42), 12.0f, App::UI_CARD, App::UI_ACCENT);
    DrawTextGdi(g, L"УМНАЯ ОЧИСТКА", App::fBodyBold, RectF((REAL)x, (REAL)rowY, (REAL)halfW, 42), App::UI_ACCENT, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(x, rowY, halfW, 42, 3201);

    int tailsX = x + halfW + 12;
    DrawRoundRect(g, RectF((REAL)tailsX, (REAL)rowY, (REAL)halfW, 42), 12.0f, App::UI_CARD, App::YELLOW);
    DrawTextGdi(g, L"ХВОСТЫ ПРИЛОЖЕНИЙ", App::fBodyBold, RectF((REAL)tailsX, (REAL)rowY, (REAL)halfW, 42), App::YELLOW, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(tailsX, rowY, halfW, 42, 3202);

    int sampY = rowY + 52;
    DrawRoundRect(g, RectF((REAL)x, (REAL)sampY, (REAL)halfW, 42), 12.0f, App::UI_CARD, App::UI_GREEN);
    DrawTextGdi(g, L"КЭШ SAMP", App::fBodyBold, RectF((REAL)x, (REAL)sampY, (REAL)halfW, 42), App::UI_GREEN, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(x, sampY, halfW, 42, 3203);

    int rightX = x + leftW + 20;
    int rightW = w - leftW - 20;
    DrawRoundRect(g, RectF((REAL)rightX, 95, (REAL)rightW, 380), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"АНАЛИЗ ДИСКА C", App::fBold, RectF((REAL)(rightX + 18), 115, (REAL)(rightW - 36), 24), App::UI_TEXT);

    double totalGB = 0, freeGB = 0;
    ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        totalGB = (double)totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
        freeGB = (double)totalFreeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
    }
    int usedPercent = (totalGB > 0) ? (int)((totalGB - freeGB) / totalGB * 100.0) : 0;
    
    int cx = rightX + rightW / 2;
    int cy = 245;
    int radius = 70;
    
    Pen bgPen(Color(255, 43, 43, 54), 16.0f); bgPen.SetStartCap(LineCapRound); bgPen.SetEndCap(LineCapRound);
    g.DrawArc(&bgPen, (REAL)(cx - radius), (REAL)(cy - radius), (REAL)(radius * 2), (REAL)(radius * 2), 0.0f, 360.0f);
    
    Pen fillPen(App::UI_ACCENT, 16.0f); fillPen.SetStartCap(LineCapRound); fillPen.SetEndCap(LineCapRound);
    g.DrawArc(&fillPen, (REAL)(cx - radius), (REAL)(cy - radius), (REAL)(radius * 2), (REAL)(radius * 2), -90.0f, (REAL)(usedPercent * 3.6f));

    wchar_t buf[128];
    swprintf(buf, 128, L"%d%%", usedPercent);
    DrawTextGdi(g, buf, App::fTitle, RectF((REAL)(cx - 60), (REAL)(cy - 15), 120, 30), App::UI_TEXT, StringAlignmentCenter);

    swprintf(buf, 128, L"Занято: %.1f ГБ", totalGB - freeGB);
    DrawTextGdi(g, buf, App::fBold, RectF((REAL)rightX, (REAL)(cy + radius + 30), (REAL)rightW, 20), App::UI_TEXT, StringAlignmentCenter);
    
    swprintf(buf, 128, L"Свободно: %.1f ГБ", freeGB);
    DrawTextGdi(g, buf, App::fSmall, RectF((REAL)rightX, (REAL)(cy + radius + 55), (REAL)rightW, 20), App::UI_MUTED, StringAlignmentCenter);
}

static std::wstring FormatElapsedAgo(uint64_t ftVal) {
    if (ftVal == 0) return L"недавно";
    uint64_t now = Journal::NowFileTimeU64();
    if (now <= ftVal) return L"только что";
    uint64_t diffSec = (now - ftVal) / 10000000ULL;
    if (diffSec < 60) return L"только что";
    uint64_t mins = diffSec / 60;
    if (mins < 60) return std::to_wstring(mins) + L"м назад";
    uint64_t hrs = mins / 60;
    if (hrs < 24) return std::to_wstring(hrs) + L"ч назад";
    return std::to_wstring(hrs / 24) + L"д назад";
}

// Модальное окно Diff-просмотра группы с 3 кнопками:
// 1 = Откатить только изменённые, 2 = Откатить всё, 0 = Отмена
static int ShowDiffModalDialog(HWND parent, const std::wstring& group, const std::vector<Journal::DiffEntry>& diffs) {
    struct DiffModalState {
        std::wstring text;
        int result = 0;
        bool done = false;
    } st;

    std::wstringstream wss;
    wss << L"Группа: [" << group << L"]\r\n\r\n";
    if (diffs.empty()) {
        wss << L"(Нет реестровых записей V3|REG_VALUE для сравнения)\r\n";
    } else {
        const size_t MAX_DIFF_LINES = 300;
        size_t count = 0;
        for (const auto& d : diffs) {
            if (count++ >= MAX_DIFF_LINES) {
                wss << L"\r\n... (и еще " << (diffs.size() - MAX_DIFF_LINES) << L" записей скрыто для предотвращения зависания UI)\r\n";
                break;
            }
            wss << (d.wouldChange ? L"[Будет изменено]  " : L"[Без изменений]   ")
                << d.hive << L"\\" << d.path << L"\\" << d.name << L": "
                << d.currentValue << L" \u2192 " << d.restoredValue << L"\r\n";
        }
    }
    st.text = wss.str();

    static const wchar_t* kDiffCls = L"LEFTIK_DIFF_MODAL_V60";
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            auto* pSt = reinterpret_cast<DiffModalState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            if (m == WM_CREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
                pSt = reinterpret_cast<DiffModalState*>(cs->lpCreateParams);
                SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)pSt);
                HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
                HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pSt ? pSt->text.c_str() : L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
                    16, 16, 728, 340, h, (HMENU)100, cs->hInstance, nullptr);
                HWND hB1 = CreateWindowExW(0, L"BUTTON", L"Откатить только изменённые",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 16, 372, 240, 36, h, (HMENU)101, cs->hInstance, nullptr);
                HWND hB2 = CreateWindowExW(0, L"BUTTON", L"Откатить всё",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 270, 372, 180, 36, h, (HMENU)102, cs->hInstance, nullptr);
                HWND hB3 = CreateWindowExW(0, L"BUTTON", L"Отмена",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 604, 372, 140, 36, h, (HMENU)103, cs->hInstance, nullptr);
                if (hFont) {
                    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                    SendMessageW(hB1, WM_SETFONT, (WPARAM)hFont, TRUE);
                    SendMessageW(hB2, WM_SETFONT, (WPARAM)hFont, TRUE);
                    SendMessageW(hB3, WM_SETFONT, (WPARAM)hFont, TRUE);
                }
                return 0;
            }
            if (m == WM_COMMAND && pSt) {
                int id = LOWORD(w);
                if (id == 101) { pSt->result = 1; pSt->done = true; DestroyWindow(h); return 0; }
                if (id == 102) { pSt->result = 2; pSt->done = true; DestroyWindow(h); return 0; }
                if (id == 103 || id == IDCANCEL) { pSt->result = 0; pSt->done = true; DestroyWindow(h); return 0; }
            }
            if (m == WM_CLOSE && pSt) {
                pSt->result = 0;
                pSt->done = true;
                DestroyWindow(h);
                return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kDiffCls;
        RegisterClassExW(&wc);
        s_reg = true;
    }

    RECT pr{};
    GetWindowRect(parent, &pr);
    int dlgW = 776, dlgH = 460;
    int dlgX = pr.left + ((pr.right - pr.left) - dlgW) / 2;
    int dlgY = pr.top + ((pr.bottom - pr.top) - dlgH) / 2;

    EnableWindow(parent, FALSE);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kDiffCls, L"LEFTIK — Diff перед откатом группы",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, dlgH, parent, nullptr, GetModuleHandleW(nullptr), &st);

    MSG msg;
    while (!st.done && IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return st.result;
}

static size_t g_lastClickedGroupIdx = 0;

void DrawRestore(Graphics& g) {
    int x = App::SIDE_W + 30, y = 40, w = App::windowW - App::SIDE_W - 60;
    DrawTextGdi(g, L"ВОССТАНОВЛЕНИЕ", App::fTitle, RectF((REAL)x, (REAL)y, (REAL)w, 40), App::UI_TEXT); y += 52;

    // 1. Создать точку восстановления
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, 76), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawIconTile(g, x + 18, y + 15, 46, L"\uE81C", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
    DrawTextGdi(g, L"Создать точку восстановления Windows", App::fBold, RectF((REAL)(x + 78), (REAL)(y + 14), (REAL)(w - 320), 24), App::UI_TEXT);
    DrawTextGdi(g, L"Делает снимок системы перед оптимизацией.", App::fSmall, RectF((REAL)(x + 78), (REAL)(y + 42), (REAL)(w - 320), 20), App::UI_MUTED);

    int btnW = 210;
    DrawRoundRect(g, RectF((REAL)(x + w - btnW - 18), (REAL)(y + 16), (REAL)btnW, 44), 10.0f, App::UI_ACCENT, App::UI_ACCENT);
    DrawTextGdi(g, L"СОЗДАТЬ ТОЧКУ", App::fBodyBold, RectF((REAL)(x + w - btnW - 18), (REAL)(y + 16), (REAL)btnW, 44), Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
    AddHit(x + w - btnW - 18, y + 16, btnW, 44, 3305);

    y += 90;

    // 2. Панель действий журнала: [ОТКАТИТЬ ВСЁ]  [ЭКСПОРТ]  [ИМПОРТ]
    int gap = 14;
    int actionBtnW = (w - gap * 2) / 3;
    int actionBtnH = 44;

    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)actionBtnW, (REAL)actionBtnH), 10.0f, App::RED, App::RED);
    DrawTextGdi(g, L"ОТКАТИТЬ ВСЁ", App::fBodyBold, RectF((REAL)x, (REAL)y, (REAL)actionBtnW, (REAL)actionBtnH), Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
    AddHit(x, y, actionBtnW, actionBtnH, 3300);

    int expX = x + actionBtnW + gap;
    DrawRoundRect(g, RectF((REAL)expX, (REAL)y, (REAL)actionBtnW, (REAL)actionBtnH), 10.0f, App::UI_CARD, App::UI_ACCENT);
    DrawTextGdi(g, L"ЭКСПОРТ ЖУРНАЛА", App::fBodyBold, RectF((REAL)expX, (REAL)y, (REAL)actionBtnW, (REAL)actionBtnH), App::UI_ACCENT, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(expX, y, actionBtnW, actionBtnH, 3307);

    int impX = expX + actionBtnW + gap;
    DrawRoundRect(g, RectF((REAL)impX, (REAL)y, (REAL)actionBtnW, (REAL)actionBtnH), 10.0f, App::UI_CARD, App::UI_GREEN);
    DrawTextGdi(g, L"ИМПОРТ ЖУРНАЛА", App::fBodyBold, RectF((REAL)impX, (REAL)y, (REAL)actionBtnW, (REAL)actionBtnH), App::UI_GREEN, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(impX, y, actionBtnW, actionBtnH, 3308);

    y += actionBtnH + 20;

    // 3. Секция ГРУППЫ ТВИКОВ (Journal 2.0)
    DrawTextGdi(g, L"── ГРУППЫ ТВИКОВ (JOURNAL 2.0) ──────────────────────────", App::fBold, RectF((REAL)x, (REAL)y, (REAL)w, 26), App::UI_TEXT);
    y += 32;

    if (App::cachedGroups.empty()) {
        App::cachedGroups = Journal::GetJournalGroups();
    }

    int bottomBtnH = 46;
    int bottomBarY = App::windowH - 55 - bottomBtnH;
    int listBoxH = std::max(90, bottomBarY - y - 14);

    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)listBoxH), 12.0f, App::UI_CARD, App::UI_BORDER);

    if (App::cachedGroups.empty()) {
        auto history = Util::GetJournalHistory();
        if (history.empty()) {
            DrawTextGdi(g, L"Групп твиков пока нет. Журнал пуст — кнопки Экспорт/Импорт готовы к работе.",
                        App::fBody, RectF((REAL)(x + 20), (REAL)(y + 18), (REAL)(w - 40), 34), App::UI_MUTED);
        } else {
            DrawTextGdi(g, L"Групп V3 нет (найдены записи V2 — используйте «ОТКАТИТЬ ВСЁ»):",
                        App::fSmall, RectF((REAL)(x + 18), (REAL)(y + 10), (REAL)(w - 36), 22), App::UI_MUTED);
            int hy = y + 36;
            int maxH = std::min((int)history.size(), (listBoxH - 44) / 28);
            for (int i = 0; i < maxH; ++i) {
                DrawTextEllipsis(g, L"• " + history[i], App::fSmall,
                                 RectF((REAL)(x + 22), (REAL)hy, (REAL)(w - 44), 24), App::UI_TEXT);
                hy += 28;
            }
        }
    } else {
        int rowY = y + 12;
        int rowH = 40;
        int maxRows = std::min((int)App::cachedGroups.size(), (listBoxH - 20) / (rowH + 6));
        for (int i = 0; i < maxRows; ++i) {
            const std::wstring& grp = App::cachedGroups[i];
            bool checked = (App::selectedGroups.find(grp) != App::selectedGroups.end());
            auto meta = Journal::GetGroupMeta(grp);

            DrawRoundRect(g, RectF((REAL)(x + 10), (REAL)rowY, (REAL)(w - 20), (REAL)rowH), 8.0f,
                          checked ? App::UI_ACCENT_DIM : App::UI_CARD_HOVER,
                          checked ? App::UI_ACCENT : Color(0, 0, 0, 0));

            int cbX = x + 22, cbY = rowY + 9;
            DrawRoundRect(g, RectF((REAL)cbX, (REAL)cbY, 22, 22), 5.0f,
                          checked ? App::UI_ACCENT : App::UI_SIDEBAR,
                          checked ? App::UI_ACCENT : App::UI_BORDER);
            if (checked) {
                DrawTextGdi(g, L"\uE73E", App::fIcon, RectF((REAL)cbX, (REAL)cbY, 22, 22),
                            Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
            }

            std::wstring label = L"[" + grp + L"]  " + meta.title;
            DrawTextEllipsis(g, label, App::fBodyBold,
                             RectF((REAL)(cbX + 32), (REAL)rowY, (REAL)(w - 310), (REAL)rowH), App::UI_TEXT);

            wchar_t rightInfo[128];
            swprintf(rightInfo, 128, L"(%d зап., %ls)", meta.entryCount, FormatElapsedAgo(meta.lastTimestamp).c_str());
            DrawTextGdi(g, rightInfo, App::fSmall,
                        RectF((REAL)(x + w - 250), (REAL)rowY, 230, (REAL)rowH),
                        App::UI_MUTED, StringAlignmentFar, StringAlignmentCenter);

            // Клик по строке кодирует индекс через 3600 + i и вызывает 3309
            AddHit(x + 10, rowY, w - 20, rowH, 3600 + i);
            rowY += rowH + 6;
        }
    }

    // 4. Нижние кнопки Selective Rollback и Diff
    int halfW = (w - gap) / 2;
    wchar_t selBtnText[96];
    swprintf(selBtnText, 96, L"ОТКАТИТЬ ВЫБРАННЫЕ (%d)", (int)App::selectedGroups.size());
    bool hasSel = !App::selectedGroups.empty();

    DrawRoundRect(g, RectF((REAL)x, (REAL)bottomBarY, (REAL)halfW, (REAL)bottomBtnH), 10.0f,
                  hasSel ? App::UI_ACCENT : App::UI_CARD,
                  hasSel ? App::UI_ACCENT : App::UI_BORDER);
    DrawTextGdi(g, selBtnText, App::fBodyBold,
                RectF((REAL)x, (REAL)bottomBarY, (REAL)halfW, (REAL)bottomBtnH),
                hasSel ? Color(255, 255, 255, 255) : App::UI_MUTED,
                StringAlignmentCenter, StringAlignmentCenter);
    AddHit(x, bottomBarY, halfW, bottomBtnH, 3310);

    int diffX = x + halfW + gap;
    DrawRoundRect(g, RectF((REAL)diffX, (REAL)bottomBarY, (REAL)halfW, (REAL)bottomBtnH), 10.0f,
                  App::UI_CARD, App::UI_ACCENT);
    DrawTextGdi(g, L"ПОКАЗАТЬ DIFF", App::fBodyBold,
                RectF((REAL)diffX, (REAL)bottomBarY, (REAL)halfW, (REAL)bottomBtnH),
                App::UI_ACCENT, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(diffX, bottomBarY, halfW, bottomBtnH, 3306);
}

int GamesMaxScroll() {
    int totalGames = (int)App::steamGames.size();
    int cols = 2;
    int rows = (totalGames + cols - 1) / cols;
    int cardH = 185;
    int totalH = rows * (cardH + 20);
    int viewTop = 110;
    int viewBottom = App::windowH - 50;
    int viewH = std::max(100, viewBottom - viewTop);
    return std::min(0, viewH - totalH);
}

void DrawGames(Graphics& g) {
    int x = App::SIDE_W + 30, y = 40, w = App::windowW - App::SIDE_W - 60;
    DrawTextGdi(g, L"ИГРОВОЙ ЦЕНТР", App::fTitle, RectF((REAL)x, (REAL)y, (REAL)w, 40), App::UI_TEXT); y += 36;
    DrawTextGdi(g, L"Выбери игру: CS2, Dota 2 и Arizona получат консольные команды. Остальные — общую оптимизацию.", App::fSmall, RectF((REAL)x, (REAL)y, (REAL)w, 20), App::UI_MUTED); y += 34;

    std::vector<App::Game> supportedGames = App::steamGames;

    if (supportedGames.empty()) {
        DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, 80), 12.0f, App::UI_CARD, App::UI_BORDER);
        DrawTextGdi(g, L"Игры не найдены. Убедитесь, что Steam установлен.", App::fBody, RectF((REAL)(x + 20), (REAL)y, (REAL)(w - 40), 80), App::UI_MUTED);
        return;
    }

    std::wstring steamMain = Util::ReadRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    std::wstring cacheDir = Util::Join(steamMain, L"appcache\\librarycache");

    int cols = 2;
    int cardW = (w - 20) / cols;
    int cardH = 185;

    int viewTop = y;
    int viewBottom = App::windowH - 50;
    int maxScroll = GamesMaxScroll();
    App::gamesScrollY = std::clamp(App::gamesScrollY, maxScroll, 0);

    Region listClip(Rect(x, viewTop, w, viewBottom - viewTop));
    g.SetClip(&listClip);

    for (size_t i = 0; i < supportedGames.size(); ++i) {
        auto& game = supportedGames[i];
        int col = (int)(i % cols);
        int row = (int)(i / cols);
        int curX = x + col * (cardW + 20);
        int curY = viewTop + row * (cardH + 20) + App::gamesScrollY;

        if (curY + cardH < viewTop || curY > viewBottom) continue;

        DrawRoundRect(g, RectF((REAL)curX, (REAL)curY, (REAL)cardW, (REAL)cardH), 12.0f, App::UI_CARD, App::UI_BORDER);

        Image* cover = nullptr;
        auto it = App::gameImages.find(game.name);
        if (it != App::gameImages.end()) {
            cover = it->second;
        } else if (game.appid > 0 && !steamMain.empty()) {
            std::wstring id = std::to_wstring(game.appid);
            std::wstring tryPaths[] = {
                Util::Join(cacheDir, id + L"_library_600x900.jpg"),
                Util::Join(cacheDir, id + L"_library_hero.jpg"),
                Util::Join(cacheDir, id + L"_header.jpg"),
            };
            for (auto& p : tryPaths) {
                Image* img = Image::FromFile(p.c_str());
                if (img && img->GetLastStatus() == Ok) { cover = img; break; }
                delete img;
            }
            // Кэшируем ТОЛЬКО успешные загрузки, чтобы не долбить диск каждый кадр
            if (cover) {
                App::gameImages[game.name] = cover;
            }
        }

        if (cover) {
            GraphicsPath coverPath;
            float d = 24.0f;
            coverPath.AddArc((REAL)curX, (REAL)curY, (REAL)d, (REAL)d, 180.0f, 90.0f);
            coverPath.AddArc((REAL)(curX + cardW - d), (REAL)curY, (REAL)d, (REAL)d, 270.0f, 90.0f);
            coverPath.AddLine((REAL)(curX + cardW), (REAL)(curY + 115), (REAL)curX, (REAL)(curY + 115));
            coverPath.CloseFigure();
            g.SetClip(&coverPath, CombineModeIntersect);
            g.DrawImage(cover, RectF((REAL)curX, (REAL)curY, (REAL)cardW, 115.0f));
            g.SetClip(&listClip);
        } else {
            int hash = 0; for (wchar_t c : game.name) hash += (int)c;
            int r = 50 + (hash % 100), gr = 50 + ((hash * 7) % 100), b = 100 + ((hash * 13) % 100);
            LinearGradientBrush coverBrush(Point(curX, curY), Point(curX + cardW, curY + 115), Color(255, r, gr, b), Color(255, r / 2, gr / 2, b / 2));
            GraphicsPath coverPath; float d = 24.0f;
            coverPath.AddArc((REAL)curX, (REAL)curY, (REAL)d, (REAL)d, 180.0f, 90.0f);
            coverPath.AddArc((REAL)(curX + cardW - d), (REAL)curY, (REAL)d, (REAL)d, 270.0f, 90.0f);
            coverPath.AddLine((REAL)(curX + cardW), (REAL)(curY + 115), (REAL)curX, (REAL)(curY + 115));
            coverPath.CloseFigure();
            g.FillPath(&coverBrush, &coverPath);
            DrawTextGdi(g, L"\uE7FC", App::fIconBig, RectF((REAL)curX, (REAL)(curY + 20), (REAL)cardW, 75), Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
        }

        DrawTextEllipsis(g, game.name, App::fBold, RectF((REAL)(curX + 15), (REAL)(curY + 116), (REAL)(cardW - 30), 24), App::UI_TEXT);

        bool isCS2 = game.name.find(L"Counter-Strike") != std::wstring::npos;
        bool isDota = game.name.find(L"Dota 2") != std::wstring::npos;
        bool isArizona = game.name.find(L"Arizona") != std::wstring::npos;

        int btnW = (isCS2 || isDota) ? (cardW - 40) / 2 : (cardW - 30);
        int btnX = curX + 15;
        int btnY = curY + cardH - 30;
        
        DrawRoundRect(g, RectF((REAL)btnX, (REAL)btnY, (REAL)btnW, 26), 12.0f, App::UI_ACCENT, App::UI_ACCENT);

        std::wstring btnText = L"ОПТИМИЗАЦИЯ";
        if (isCS2) btnText = L"КОНСОЛЬ CS2";
        else if (isDota) btnText = L"КОНСОЛЬ DOTA 2";
        else if (isArizona) btnText = L"НАСТРОИТЬ ARIZONA";

        DrawTextGdi(g, btnText, App::fSmall, RectF((REAL)btnX, (REAL)btnY, (REAL)btnW, 26), Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
        if (btnY >= viewTop && btnY + 26 <= viewBottom)
            AddHit(btnX, btnY, btnW, 26, 4000 + (int)i);

        if (isCS2 || isDota) {
            DrawRoundRect(g, RectF((REAL)(btnX + btnW + 10), (REAL)btnY, (REAL)btnW, 26), 12.0f, App::UI_CARD_HOVER, App::UI_BORDER);
            DrawTextGdi(g, L"КОНФИГ", App::fSmall, RectF((REAL)(btnX + btnW + 10), (REAL)btnY, (REAL)btnW, 26), App::UI_TEXT, StringAlignmentCenter, StringAlignmentCenter);
            if (btnY >= viewTop && btnY + 26 <= viewBottom)
                AddHit(btnX + btnW + 10, btnY, btnW, 26, 4500 + (int)i);
        }
        if (App::hwInfo.isIntelPlatform && (isCS2 || isDota)) {
            // 2a.4: только информирование, без клика. Детект APO не выполняется:
            // имя службы не подтверждено, APO ставится внешне через Intel DSA.
            int apoW = cardW - 30;
            int apoY = btnY - (isCS2 ? 70 : 38); // CS2: над гайд-кнопкой, Dota: над кнопками
            DrawRoundRect(g, RectF((REAL)btnX, (REAL)apoY, (REAL)apoW, 34), 10.0f,
                          Color(255, 30, 50, 30), Color(255, 60, 120, 60));
            const wchar_t* apoTxt =
                L"На Intel 12th gen+ Intel APO даёт до 27% в CS2. Проверьте в Intel DSA, установлен ли APO — LEFTIK его не дублирует.";
            DrawTextGdi(g, apoTxt, App::fSmall,
                        RectF((REAL)(btnX + 6), (REAL)apoY, (REAL)(apoW - 12), 34),
                        Color(255, 100, 200, 100),
                        StringAlignmentCenter, StringAlignmentCenter);
        }
        if (isCS2) {
            int guideW = cardW - 30;
            int guideY = btnY - 32;
            DrawRoundRect(g, RectF((REAL)btnX, (REAL)guideY, (REAL)guideW, 24), 10.0f, App::UI_CARD, App::UI_ACCENT);
            DrawTextGdi(g, L"ГАЙД: Reflex / Anti-Lag 2", App::fSmall, RectF((REAL)btnX, (REAL)guideY, (REAL)guideW, 24), App::UI_ACCENT, StringAlignmentCenter, StringAlignmentCenter);
            if (guideY >= viewTop && guideY + 24 <= viewBottom)
                AddHit(btnX, guideY, guideW, 24, 4600 + (int)i);
        }
    }
    g.ResetClip();
}


void DrawCpuGraph(Graphics& g, int gX, int gY, int gW, int gH) {
    Pen gridPen(Color(25, 255, 255, 255), 1.0f);
    for (int i = 0; i <= 4; i++)
        g.DrawLine(&gridPen, gX, gY + i * (gH / 4), gX + gW, gY + i * (gH / 4));

    GraphicsPath path; GraphicsPath fillPath;
    bool first = true; float prevX = 0.0f, prevY = 0.0f;
    for (int i = 0; i < 30; ++i) {
        int idx = (App::cpuHistoryIdx - 30 + i + 60) % 60;
        float val = App::cpuHistory[idx] / 100.0f;
        float px = gX + (i * (gW / 29.0f));
        float py = gY + gH - (val * gH);
        if (first) {
            path.StartFigure();
            path.AddLine((REAL)px, (REAL)py, (REAL)px, (REAL)py);
            fillPath.StartFigure();
            fillPath.AddLine((REAL)px, (REAL)(gY + gH), (REAL)px, (REAL)py);
            first = false;
        } else {
            path.AddLine((REAL)prevX, (REAL)prevY, (REAL)px, (REAL)py);
            fillPath.AddLine((REAL)prevX, (REAL)prevY, (REAL)px, (REAL)py);
        }
        prevX = px; prevY = py;
    }
    fillPath.AddLine((REAL)prevX, (REAL)prevY, (REAL)prevX, (REAL)(gY + gH));
    fillPath.CloseFigure();
    LinearGradientBrush fillBrush(Point(gX, gY), Point(gX, gY + gH), Color(80, 0, 230, 118), Color(0, 0, 230, 118));
    g.FillPath(&fillBrush, &fillPath);
    Pen linePen(App::UI_GREEN, 2.0f);
    g.DrawPath(&linePen, &path);
}

void DrawDashboard(Graphics& g) {
    int x = App::SIDE_W + 30, y = 32, w = App::windowW - App::SIDE_W - 60;

    // v58.0: два переключателя в правом верхнем углу — трей и авто-очистка standby.
    {
        int cbX = x + w - 248, cbY = y + 6;
        DrawRoundRect(g, RectF((REAL)cbX, (REAL)cbY, 22, 22), 6.0f,
                      App::backgroundMode ? App::UI_ACCENT : App::UI_SIDEBAR,
                      App::backgroundMode ? App::UI_ACCENT : App::UI_BORDER);
        if (App::backgroundMode)
            DrawTextGdi(g, L"\uE73E", App::fIcon, RectF((REAL)cbX, (REAL)cbY, 22, 22), Color(255, 255, 255, 255),
                        StringAlignmentCenter, StringAlignmentCenter);
        DrawTextGdi(g, L"Работать в фоне (Трей)", App::fSmall,
                    RectF((REAL)(cbX + 30), (REAL)cbY, 210, 22), App::UI_TEXT);
        AddHit(cbX, cbY, 240, 22, 3050);
    }
    {
        // Feature B: авто-очистка standby. Показываем только если диск SSD
        // (на HDD/неизвестном типе — бесполезно или вредно).
        int cbX = x + w - 248, cbY = y + 34;
        bool available = App::hwInfo.diskIsSSD;
        DrawRoundRect(g, RectF((REAL)cbX, (REAL)cbY, 22, 22), 6.0f,
                      (available && App::autoStandbyClean) ? App::UI_ACCENT : App::UI_SIDEBAR,
                      (available && App::autoStandbyClean) ? App::UI_ACCENT : App::UI_BORDER);
        if (available && App::autoStandbyClean)
            DrawTextGdi(g, L"\uE73E", App::fIcon, RectF((REAL)cbX, (REAL)cbY, 22, 22), Color(255, 255, 255, 255),
                        StringAlignmentCenter, StringAlignmentCenter);
        wchar_t label[64];
        swprintf(label, 64, L"Standby каждые %d мин", App::standbyIntervalMin);
        DrawTextGdi(g, label, App::fSmall,
                    RectF((REAL)(cbX + 30), (REAL)cbY, 175, 22),
                    available ? App::UI_TEXT : App::UI_MUTED);
        if (available) {
            AddHit(cbX, cbY, 200, 22, 3051);
            // Кнопка циклической смены интервала (30 -> 60 -> 120 -> 240 -> 30)
            DrawRoundRect(g, RectF((REAL)(cbX + 206), (REAL)cbY, 34, 22), 6.0f, App::UI_CARD_HOVER, App::UI_BORDER);
            DrawTextGdi(g, L"⟳", App::fSmall, RectF((REAL)(cbX + 206), (REAL)cbY, 34, 22), App::UI_ACCENT,
                        StringAlignmentCenter, StringAlignmentCenter);
            AddHit(cbX + 206, cbY, 34, 22, 3052);
        }
    }

    DrawTextGdi(g, L"МОНИТОРИНГ И ОПТИМИЗАЦИЯ", App::fH1, RectF((REAL)x, (REAL)y, (REAL)(w - 260), 34), App::UI_TEXT);
    y += 36;

    double s_junkGB = Monitor::junkGB.load(std::memory_order_relaxed);
    wchar_t junkBuf[128];
    swprintf(junkBuf, 128, L"Система работает в штатном режиме. Обнаружено %.2f ГБ мусора.", s_junkGB);
    DrawTextGdi(g, junkBuf, App::fSmall, RectF((REAL)x, (REAL)y, (REAL)w, 20), App::UI_MUTED);
    y += 30;

    // --- 4 ровные карточки в ряд: CPU, RAM, Disk, GPU ---
    int gap = 16;
    int cardW = (w - gap * 3) / 4;
    int cardH = 175;

    // 1. CPU
    int cx1 = x;
    DrawRoundRect(g, RectF((REAL)cx1, (REAL)y, (REAL)cardW, (REAL)cardH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawIconTile(g, cx1 + 16, y + 16, 40, L"\uE950", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
    DrawTextEllipsis(g, L"Процессор (CPU)", App::fBodyBold, RectF((REAL)(cx1 + 66), (REAL)(y + 16), (REAL)(cardW - 80), 20), App::UI_TEXT);
    {
        wchar_t tbuf[32];
        if (App::cpuTemp > 0) swprintf(tbuf, 32, L"%d°C", App::cpuTemp);
        else swprintf(tbuf, 32, L"Ядра: %ls", App::hwInfo.cpuCores.c_str());
        DrawTextEllipsis(g, tbuf, App::fSmall, RectF((REAL)(cx1 + 66), (REAL)(y + 36), (REAL)(cardW - 80), 18), App::UI_MUTED);
    }
    DrawTextGdi(g, App::cpuText, App::fMetric, RectF((REAL)(cx1 + 16), (REAL)(y + 68), (REAL)(cardW - 32), 40), App::UI_TEXT);
    DrawTextEllipsis(g, App::cpuName, App::fSmall, RectF((REAL)(cx1 + 16), (REAL)(y + 114), (REAL)(cardW - 32), 18), App::UI_MUTED);
    DrawProgressBar(g, cx1 + 16, y + cardH - 22, cardW - 32, 6, App::cpuLoad / 100.0,
                    App::cpuLoad < 70 ? App::UI_GREEN : (App::cpuLoad < 90 ? App::YELLOW : App::RED));

    // 2. RAM
    int cx2 = cx1 + cardW + gap;
    DrawRoundRect(g, RectF((REAL)cx2, (REAL)y, (REAL)cardW, (REAL)cardH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawIconTile(g, cx2 + 16, y + 16, 40, L"\uE964", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
    DrawTextEllipsis(g, L"Память (RAM)", App::fBodyBold, RectF((REAL)(cx2 + 66), (REAL)(y + 16), (REAL)(cardW - 80), 20), App::UI_TEXT);
    DrawTextEllipsis(g, App::ramText, App::fSmall, RectF((REAL)(cx2 + 66), (REAL)(y + 36), (REAL)(cardW - 80), 18), App::UI_MUTED);
    {
        wchar_t rbuf[32]; swprintf(rbuf, 32, L"%d%%", App::ramLoad);
        DrawTextGdi(g, rbuf, App::fMetric, RectF((REAL)(cx2 + 16), (REAL)(y + 68), 110, 40), App::UI_TEXT);
    }
    {
        int btnRamW = 104, btnRamH = 24;
        int btnRamX = cx2 + cardW - btnRamW - 16, btnRamY = y + 76;
        DrawRoundRect(g, RectF((REAL)btnRamX, (REAL)btnRamY, (REAL)btnRamW, (REAL)btnRamH), 8.0f, App::UI_ACCENT_DIM, App::UI_ACCENT);
        DrawTextGdi(g, L"ОЧИСТИТЬ ОЗУ", App::fSmall, RectF((REAL)btnRamX, (REAL)btnRamY, (REAL)btnRamW, (REAL)btnRamH), App::UI_TEXT, StringAlignmentCenter, StringAlignmentCenter);
        AddHit(btnRamX, btnRamY, btnRamW, btnRamH, 3100);
    }
    DrawTextEllipsis(g, L"Всего: " + App::totalRamText, App::fSmall, RectF((REAL)(cx2 + 16), (REAL)(y + 114), (REAL)(cardW - 32), 18), App::UI_MUTED);
    DrawProgressBar(g, cx2 + 16, y + cardH - 22, cardW - 32, 6, App::ramLoad / 100.0,
                    App::ramLoad < 75 ? App::UI_ACCENT : (App::ramLoad < 90 ? App::YELLOW : App::RED));

    // 3. Disk (Все локальные диски DRIVE_FIXED)
    int cx3 = cx2 + cardW + gap;
    DrawRoundRect(g, RectF((REAL)cx3, (REAL)y, (REAL)cardW, (REAL)cardH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawIconTile(g, cx3 + 16, y + 16, 40, L"\uEDA2", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);

    struct DashboardDrive {
        wchar_t letter;
        std::wstring root;
        double totalGB;
        double freeGB;
        int usedPct;
    };
    std::vector<DashboardDrive> driveList;
    wchar_t dBufStr[512] = {0};
    DWORD dLen = GetLogicalDriveStringsW(512, dBufStr);
    if (dLen > 0 && dLen < 512) {
        for (wchar_t* p = dBufStr; *p; p += wcslen(p) + 1) {
            if (GetDriveTypeW(p) == DRIVE_FIXED) {
                ULARGE_INTEGER fb, tb, tf;
                if (GetDiskFreeSpaceExW(p, &fb, &tb, &tf) && tb.QuadPart > 0) {
                    double tot = (double)tb.QuadPart / (1024.0 * 1024.0 * 1024.0);
                    double fr = (double)tf.QuadPart / (1024.0 * 1024.0 * 1024.0);
                    int pct = (tot > 0.0) ? (int)(((tot - fr) / tot) * 100.0) : 0;
                    driveList.push_back({ p[0], p, tot, fr, pct });
                }
            }
        }
    }
    if (driveList.empty()) {
        ULARGE_INTEGER fb, tb, tf;
        double tot = 0.0, fr = 0.0;
        if (GetDiskFreeSpaceExW(L"C:\\", &fb, &tb, &tf) && tb.QuadPart > 0) {
            tot = (double)tb.QuadPart / (1024.0 * 1024.0 * 1024.0);
            fr = (double)tf.QuadPart / (1024.0 * 1024.0 * 1024.0);
        }
        int pct = (tot > 0.0) ? (int)(((tot - fr) / tot) * 100.0) : 0;
        driveList.push_back({ L'C', L"C:\\", tot, fr, pct });
    }

    std::wstring dTitle = (driveList.size() == 1)
        ? (L"Диск (" + std::wstring(1, driveList[0].letter) + L":)")
        : (L"Диски (" + std::to_wstring(driveList.size()) + L")");
    DrawTextEllipsis(g, dTitle, App::fBodyBold, RectF((REAL)(cx3 + 66), (REAL)(y + 16), (REAL)(cardW - 80), 20), App::UI_TEXT);
    DrawTextEllipsis(g, App::hwInfo.diskIsSSD ? L"Локальные накопители (SSD)" : L"Локальные накопители", App::fSmall,
                     RectF((REAL)(cx3 + 66), (REAL)(y + 36), (REAL)(cardW - 80), 18), App::UI_MUTED);

    int maxShow = std::min((int)driveList.size(), 4);
    int availableH = cardH - 68;
    int rowH = availableH / maxShow;
    int curRowY = y + 60;

    for (int di = 0; di < maxShow; ++di) {
        const auto& d = driveList[di];
        wchar_t dline[64];
        swprintf(dline, 64, L"Диск %c: %.1f из %.0f ГБ (%d%%)", d.letter, d.freeGB, d.totalGB, d.usedPct);
        DrawTextEllipsis(g, dline, App::fSmall, RectF((REAL)(cx3 + 16), (REAL)curRowY, (REAL)(cardW - 32), 15), App::UI_TEXT);
        DrawProgressBar(g, cx3 + 16, curRowY + 16, cardW - 32, 4, d.usedPct / 100.0,
                        d.usedPct < 80 ? App::UI_GREEN : (d.usedPct < 92 ? App::YELLOW : App::RED));
        curRowY += rowH;
    }

    // 4. GPU
    int cx4 = cx3 + cardW + gap;
    DrawRoundRect(g, RectF((REAL)cx4, (REAL)y, (REAL)cardW, (REAL)cardH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawIconTile(g, cx4 + 16, y + 16, 40, L"\uE7F4", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
    DrawTextEllipsis(g, L"Видеокарта (GPU)", App::fBodyBold, RectF((REAL)(cx4 + 66), (REAL)(y + 16), (REAL)(cardW - 80), 20), App::UI_TEXT);
    {
        wchar_t gsub[64];
        if (App::gpuVramTotal > 0.01) swprintf(gsub, 64, L"VRAM: %.1f / %.0f ГБ", App::gpuVramUsed, App::gpuVramTotal);
        else if (App::gpuTemp > 0) swprintf(gsub, 64, L"Темп: %d°C", App::gpuTemp);
        else swprintf(gsub, 64, L"Адаптер активен");
        DrawTextEllipsis(g, gsub, App::fSmall, RectF((REAL)(cx4 + 66), (REAL)(y + 36), (REAL)(cardW - 80), 18), App::UI_MUTED);
    }
    {
        wchar_t gmetric[32];
        if (App::gpuLoad >= 0) swprintf(gmetric, 32, L"%d%%", App::gpuLoad);
        else swprintf(gmetric, 32, L"0%%");
        DrawTextGdi(g, gmetric, App::fMetric, RectF((REAL)(cx4 + 16), (REAL)(y + 68), (REAL)(cardW - 32), 40), App::UI_TEXT);
    }
    DrawTextEllipsis(g, App::gpuName, App::fSmall, RectF((REAL)(cx4 + 16), (REAL)(y + 114), (REAL)(cardW - 32), 18), App::UI_MUTED);
    double gpuFrac = (App::gpuLoad >= 0) ? (App::gpuLoad / 100.0) : (App::gpuVramTotal > 0.01 ? App::gpuVramUsed / App::gpuVramTotal : 0.05);
    DrawProgressBar(g, cx4 + 16, y + cardH - 22, cardW - 32, 6, gpuFrac, App::UI_GREEN);

    // --- Блок "Состояние системы" ---
    y += cardH + 20;
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, 84), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawRoundRect(g, RectF((REAL)(x + 20), (REAL)(y + 20), 44, 44), 22.0f, App::UI_GREEN_DIM, App::UI_GREEN);
    DrawTextGdi(g, L"\uE73E", App::fIconBig, RectF((REAL)(x + 20), (REAL)(y + 20), 44, 44), App::UI_GREEN, StringAlignmentCenter, StringAlignmentCenter);
    DrawTextGdi(g, L"Состояние системы", App::fSmall, RectF((REAL)(x + 80), (REAL)(y + 18), 260, 18), App::UI_MUTED);
    DrawTextGdi(g, L"Проблем не обнаружено", App::fH2, RectF((REAL)(x + 80), (REAL)(y + 38), 320, 26), App::UI_GREEN);

    int statW = 190;
    int statX1 = x + w - statW * 2 - 20;
    int statX2 = x + w - statW - 10;
    DrawTextGdi(g, L"Фоновые процессы", App::fSmall, RectF((REAL)statX1, (REAL)(y + 18), (REAL)statW, 18), App::UI_MUTED);
    wchar_t procBuf[64]; swprintf(procBuf, 64, L"%d активно", App::realProcessCount);
    DrawTextGdi(g, procBuf, App::fBold, RectF((REAL)statX1, (REAL)(y + 40), (REAL)statW, 24), App::UI_TEXT);

    DrawTextGdi(g, L"Автозапуск", App::fSmall, RectF((REAL)statX2, (REAL)(y + 18), (REAL)statW, 18), App::UI_MUTED);
    wchar_t startBuf[64]; swprintf(startBuf, 64, L"%d приложений", App::realStartupCount);
    DrawTextGdi(g, startBuf, App::fBold, RectF((REAL)statX2, (REAL)(y + 40), (REAL)statW, 24), App::UI_TEXT);

    // --- ТУРБО-РЕЖИМ (большая кнопка) ---
    {
        int turboY = y + 104;
        int turboH = 110;
        DrawRoundRect(g, RectF((REAL)x, (REAL)turboY, (REAL)w, (REAL)turboH),
                      12.0f, Color(255, 40, 22, 30), App::turboBoostActive ? App::UI_GREEN : App::RED);
        DrawIconTile(g, x + 20, turboY + 24, 52, L"\uE945", Color(255, 60, 25, 30), App::RED, App::fIconBig);
        DrawTextGdi(g, App::turboBoostActive ? L"⚡ ТУРБО-РЕЖИМ АКТИВЕН" : L"⚡ ТУРБО-РЕЖИМ",
                    App::fH1, RectF((REAL)(x + 86), (REAL)(turboY + 16), (REAL)(w - 380), 32),
                    App::turboBoostActive ? App::UI_GREEN : App::RED);
        DrawTextEllipsis(g,
            App::turboBoostActive ? L"ПК работает в режиме максимальной производительности."
                                  : L"~25 твиков, деблоат, VBS off, приоритеты CPU/GPU. Требует перезагрузки. Откат в один клик.",
            App::fSmall, RectF((REAL)(x + 86), (REAL)(turboY + 52), (REAL)(w - 380), 20), App::UI_MUTED);
        DrawTextEllipsis(g, L"🛡️ Анти-чит проверка · Hardware Guard · Автооткат через журнал",
            App::fSmall, RectF((REAL)(x + 86), (REAL)(turboY + 74), (REAL)(w - 380), 18), App::UI_GREEN);

        int tbW = 260, tbH = 64;
        int tbX = x + w - tbW - 20, tbY = turboY + (turboH - tbH) / 2;
        if (App::turboBoostActive) {
            DrawRoundRect(g, RectF((REAL)tbX, (REAL)tbY, (REAL)tbW, (REAL)tbH), 10.0f, App::UI_CARD_HOVER, App::UI_GREEN);
            DrawTextGdi(g, L"УЖЕ АКТИВЕН", App::fBodyBold, RectF((REAL)tbX, (REAL)tbY, (REAL)tbW, (REAL)tbH),
                        App::UI_GREEN, StringAlignmentCenter, StringAlignmentCenter);
        } else {
            DrawRoundRect(g, RectF((REAL)tbX, (REAL)tbY, (REAL)tbW, (REAL)tbH), 10.0f, App::RED, App::RED);
            DrawTextGdi(g, L"ЗАПУСТИТЬ ТУРБО", App::fH2, RectF((REAL)tbX, (REAL)tbY, (REAL)tbW, (REAL)tbH),
                        Color(255,255,255,255), StringAlignmentCenter, StringAlignmentCenter);
            AddHit(tbX, tbY, tbW, tbH, 3002);
        }
        y = turboY + turboH - 88;
    }

    // --- Xbox mode (2a.3) ---
    // Только Win11 24H2+ с установленным Microsoft.GamingApp. LEFTIK не дублирует
    // режим и ничего не пишет в реестр: кнопка открывает штатные настройки Windows.
    if (App::hwInfo.xboxModeAvailable) {
        y += 104;
        int xmY = y;
        DrawRoundRect(g, RectF((REAL)x, (REAL)xmY, (REAL)w, 72), 12.0f, App::UI_CARD, App::UI_BORDER);
        DrawIconTile(g, x + 20, xmY + 14, 44, L"\uE7FC",
                     App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
        DrawTextGdi(g, L"Xbox mode (штатный режим Windows)",
                    App::fH2, RectF((REAL)(x + 80), (REAL)(xmY + 12), (REAL)(w - 380), 26), App::UI_TEXT);
        DrawTextEllipsis(g,
            L"Система умеет освобождать ~2 ГБ RAM без LEFTIK. "
            L"LEFTIK не дублирует его — включите штатно в настройках Windows.",
            App::fSmall, RectF((REAL)(x + 80), (REAL)(xmY + 42), (REAL)(w - 380), 20), App::UI_MUTED);

        int xbW = 220, xbH = 40, xbX = x + w - xbW - 20, xbY = xmY + 16;
        DrawRoundRect(g, RectF((REAL)xbX, (REAL)xbY, (REAL)xbW, (REAL)xbH), 10.0f,
                      App::UI_CARD_HOVER, App::UI_ACCENT);
        DrawTextGdi(g, L"Открыть настройки Xbox mode", App::fBodyBold,
                    RectF((REAL)xbX, (REAL)xbY, (REAL)xbW, (REAL)xbH), App::UI_ACCENT,
                    StringAlignmentCenter, StringAlignmentCenter);
        AddHit(xbX, xbY, xbW, xbH, 3053);
        y += 72 + 16;
    } else {
        y += 104;
    }

    // --- Блок "Умная оптимизация в 1 клик" + большая кнопка "Начать оптимизацию" ---
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, 96), 12.0f, App::UI_CARD, App::UI_ACCENT);
    DrawIconTile(g, x + 20, y + 24, 48, L"\uE945", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
    DrawTextGdi(g, L"Умная оптимизация в 1 клик", App::fH2, RectF((REAL)(x + 84), (REAL)(y + 20), (REAL)(w - 520), 26), App::UI_TEXT);
    DrawTextEllipsis(g, L"Базовые твики, закрытие фоновых приложений, таймер 0.5ms и очистка ОЗУ.", App::fBody,
                     RectF((REAL)(x + 84), (REAL)(y + 50), (REAL)(w - 520), 22), App::UI_MUTED);

    int btnW = 240, btnH = 52;
    int smartX = x + w - btnW - 20, smartY = y + (96 - btnH) / 2;
    DrawRoundRect(g, RectF((REAL)smartX, (REAL)smartY, (REAL)btnW, (REAL)btnH), 12.0f, App::UI_ACCENT, App::UI_ACCENT);
    DrawTextGdi(g, L"НАЧАТЬ ОПТИМИЗАЦИЮ", App::fBold, RectF((REAL)smartX, (REAL)smartY, (REAL)btnW, (REAL)btnH),
                Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
    AddHit(smartX, smartY, btnW, btnH, 3000);

    int magicW = 165;
    int magicX = smartX - magicW - 12;
    DrawRoundRect(g, RectF((REAL)magicX, (REAL)smartY, (REAL)magicW, (REAL)btnH), 12.0f, App::UI_CARD_HOVER, App::UI_BORDER);
    DrawTextGdi(g, L"Игровой профиль", App::fBodyBold, RectF((REAL)magicX, (REAL)smartY, (REAL)magicW, (REAL)btnH),
                App::UI_TEXT, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(magicX, smartY, magicW, btnH, 3001);

    // --- 3 карточки в один ряд: PRO, ULTRA и Замер GPU (без выхода за нижнюю границу окна) ---
    y += 116;
    DrawTextGdi(g, L"Режимы производительности и мониторинг GPU", App::fBold, RectF((REAL)x, (REAL)y, (REAL)w, 26), App::UI_TEXT);
    y += 32;

    int modeCardW = (w - 2 * 16) / 3;
    int modeCardH = 92;

    // Карточка 1: PRO Высокий
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)modeCardW, (REAL)modeCardH), 12.0f, App::UI_CARD,
                  App::isMaxActive ? App::UI_GREEN : App::UI_BORDER);
    DrawIconTile(g, x + 16, y + 22, 48, L"\uE945", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
    DrawTextEllipsis(g, L"PRO Высокий", App::fH2,
                     RectF((REAL)(x + 76), (REAL)(y + 18), (REAL)(modeCardW - 148), 26), App::UI_TEXT);
    DrawTextEllipsis(g, L"Таймер 0.5ms + очистка standby без отключения защиты", App::fSmall,
                     RectF((REAL)(x + 76), (REAL)(y + 48), (REAL)(modeCardW - 92), 20), App::UI_MUTED);
    DrawToggleSwitch(g, x + modeCardW - 60, y + 20, 44, 24, App::isMaxActive, App::UI_GREEN);
    AddHit(x, y, modeCardW, modeCardH, 3101);

    // Карточка 2: ULTRA
    int xRight2 = x + modeCardW + 16;
    DrawRoundRect(g, RectF((REAL)xRight2, (REAL)y, (REAL)modeCardW, (REAL)modeCardH), 12.0f, App::UI_CARD,
                  App::isUltraActive ? App::UI_GREEN : App::UI_BORDER);
    DrawIconTile(g, xRight2 + 16, y + 22, 48, L"\uE7FC", App::UI_GREEN_DIM, App::UI_GREEN, App::fIconBig);
    DrawTextEllipsis(g, L"ULTRA", App::fH2,
                     RectF((REAL)(xRight2 + 76), (REAL)(y + 18), (REAL)(modeCardW - 148), 26), App::UI_TEXT);
    DrawTextEllipsis(g, L"Приоритет CPU/GPU, GameDVR и HAGS с откатом", App::fSmall,
                     RectF((REAL)(xRight2 + 76), (REAL)(y + 48), (REAL)(modeCardW - 92), 20), App::UI_MUTED);
    DrawToggleSwitch(g, xRight2 + modeCardW - 60, y + 20, 44, 24, App::isUltraActive, App::UI_GREEN);
    AddHit(xRight2, y, modeCardW, modeCardH, 3102);

    // Карточка 3: Замер GPU
    int xRight3 = xRight2 + modeCardW + 16;
    bool monRun = FpsMonitor::running.load();
    DrawRoundRect(g, RectF((REAL)xRight3, (REAL)y, (REAL)modeCardW, (REAL)modeCardH), 12.0f, App::UI_CARD,
                  monRun ? App::UI_GREEN : App::UI_BORDER);
    DrawIconTile(g, xRight3 + 16, y + 22, 48, L"\uE9D9", App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIconBig);
    DrawTextEllipsis(g, L"Замер GPU", App::fH2,
                     RectF((REAL)(xRight3 + 76), (REAL)(y + 18), (REAL)(modeCardW - 170), 26), App::UI_TEXT);
    if (monRun) {
        wchar_t buf[128];
        swprintf(buf, 128, L"Сейчас: %.0f%% | Средн: %.0f%% (%d)",
                 FpsMonitor::currentFps.load(), FpsMonitor::avgFps.load(), FpsMonitor::samples.load());
        DrawTextEllipsis(g, buf, App::fSmall,
                         RectF((REAL)(xRight3 + 76), (REAL)(y + 48), (REAL)(modeCardW - 92), 20), App::UI_GREEN);
        DrawRoundRect(g, RectF((REAL)(xRight3 + modeCardW - 86), (REAL)(y + 18), 70, 26), 6.0f, App::RED, App::RED);
        DrawTextGdi(g, L"СТОП", App::fSmall,
                    RectF((REAL)(xRight3 + modeCardW - 86), (REAL)(y + 18), 70, 26),
                    Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
        AddHit(xRight3, y, modeCardW, modeCardH, 3501);
    } else {
        DrawTextEllipsis(g, L"Замер нагрузки GPU во время игры", App::fSmall,
                         RectF((REAL)(xRight3 + 76), (REAL)(y + 48), (REAL)(modeCardW - 92), 20), App::UI_MUTED);
        DrawRoundRect(g, RectF((REAL)(xRight3 + modeCardW - 86), (REAL)(y + 18), 70, 26), 6.0f, App::UI_ACCENT, App::UI_ACCENT);
        DrawTextGdi(g, L"СТАРТ", App::fSmall,
                    RectF((REAL)(xRight3 + modeCardW - 86), (REAL)(y + 18), 70, 26),
                    Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
        AddHit(xRight3, y, modeCardW, modeCardH, 3500);
    }
}

void DrawBottomBar(Graphics& g) {
    int barH = 40;
    int barY = App::windowH - barH;
    
    SolidBrush bgBrush(App::UI_SIDEBAR); 
    g.FillRectangle(&bgBrush, 0, barY, App::windowW, barH);
    
    Pen linePen(App::UI_BORDER, 1.0f);
    g.DrawLine(&linePen, 0, barY, App::windowW, barY);

    std::wstring modeText = L"⚡ Режим: Стандартный (Auto-Guard ON)";
    Color modeColor = App::UI_TEXT;
    
    if (App::isUltraActive) {
        modeText = L"🎮 Режим: Игровой (ULTRA)";
        modeColor = App::UI_GREEN;
    } else if (App::isMaxActive) {
        modeText = L"⚡ Режим: Максимальная оптимизация";
        modeColor = App::UI_ACCENT;
    } else if (App::magicBoostActive) {
        modeText = L"🚀 MAX FPS буст активен";
        modeColor = App::UI_ACCENT;
    } else if (Monitor::gameActive.load(std::memory_order_relaxed)) {
        modeText = L"🎮 АВТО-БУСТ ИГРЫ АКТИВЕН";
        modeColor = App::UI_GREEN;
    }

    DrawTextGdi(g, modeText, App::fSmall, RectF(30, (REAL)barY, 320, (REAL)barH), modeColor);

    SYSTEM_POWER_STATUS sps{};
    std::wstring powerText = L"Питание: н/д";
    Color powerColor = App::UI_MUTED;
    if (GetSystemPowerStatus(&sps)) {
        if (sps.ACLineStatus == 1) {
            powerText = L"🔌 Сеть";
            powerColor = App::UI_GREEN;
        } else if (sps.ACLineStatus == 0) {
            if (sps.BatteryLifePercent != 255) {
                wchar_t pbuf[64];
                swprintf(pbuf, 64, L"🔋 Батарея %d%%", (int)sps.BatteryLifePercent);
                powerText = pbuf;
                powerColor = sps.BatteryLifePercent <= 20 ? App::RED : App::YELLOW;
            } else {
                powerText = L"🔋 Батарея";
                powerColor = App::YELLOW;
            }
        }
    }
    DrawTextGdi(g, powerText, App::fSmall, RectF(360, (REAL)barY, 220, (REAL)barH), powerColor);

    if (TimerMgr::OwnerCount() > 0)
        DrawTextGdi(g, L"⏱ Таймер 0.5ms активен", App::fSmall, RectF((REAL)(App::windowW - 430), (REAL)barY, 210, (REAL)barH), App::UI_ACCENT, StringAlignmentFar);
    DrawTextGdi(g, L"🛡️ Система защищена", App::fSmall, RectF((REAL)(App::windowW - 200), (REAL)barY, 170, (REAL)barH), App::UI_GREEN, StringAlignmentFar);
}

void DrawDiagnostics(Graphics& g) {
    int x = App::SIDE_W + 30, y = 40, w = App::windowW - App::SIDE_W - 60;
    DrawTextGdi(g, L"ДИАГНОСТИКА", App::fTitle, RectF((REAL)x, (REAL)y, (REAL)(w - 260), 40), App::UI_TEXT);

    int btnW = 210;
    int btnExtW = 180;
    int btnExtX = x + w - btnW - btnExtW - 10;
    DrawRoundRect(g, RectF((REAL)btnExtX, (REAL)(y + 3), (REAL)btnExtW, 36), 10.0f, App::UI_CARD, App::UI_GREEN);
    DrawTextGdi(g, L"БУСТ-СОФТ (ГАЙД)", App::fBodyBold, RectF((REAL)btnExtX, (REAL)(y + 3), (REAL)btnExtW, 36), App::UI_GREEN, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(btnExtX, y + 3, btnExtW, 36, 3401);

    DrawRoundRect(g, RectF((REAL)(x + w - btnW), (REAL)(y + 3), (REAL)btnW, 36), 10.0f, App::UI_CARD, App::UI_ACCENT);
    DrawTextGdi(g, L"ПЕРЕСКАНИРОВАТЬ", App::fBodyBold, RectF((REAL)(x + w - btnW), (REAL)(y + 3), (REAL)btnW, 36), App::UI_ACCENT, StringAlignmentCenter, StringAlignmentCenter);
    AddHit(x + w - btnW, y + 3, btnW, 36, 3400);
    y += 52;

    // Плоская сетка карточек железа (2 колонки x 4 ряда)
    int colGap = 16, rowGap = 10;
    int colW = (w - colGap) / 2;
    int tileH = 54;

    auto DrawHwTile = [&](int col, int row, const wchar_t* icon, const wchar_t* label, const std::wstring& value, Color valueColor) {
        int tx = x + col * (colW + colGap);
        int ty = y + row * (tileH + rowGap);
        DrawRoundRect(g, RectF((REAL)tx, (REAL)ty, (REAL)colW, (REAL)tileH), 10.0f, App::UI_CARD, App::UI_BORDER);
        DrawIconTile(g, tx + 12, ty + 9, 36, icon, App::UI_ACCENT_DIM, App::UI_ACCENT, App::fIcon);
        DrawTextGdi(g, label, App::fSmall, RectF((REAL)(tx + 58), (REAL)(ty + 8), (REAL)(colW - 70), 16), App::UI_MUTED);
        DrawTextEllipsis(g, value, App::fBodyBold, RectF((REAL)(tx + 58), (REAL)(ty + 26), (REAL)(colW - 70), 20), valueColor);
    };

    wchar_t buf[512];
    std::wstring cpuText = App::cpuName;
    if (!App::hwInfo.cpuCores.empty()) cpuText += L"  (" + App::hwInfo.cpuCores + L")";
    DrawHwTile(0, 0, L"\uE950", L"Процессор", cpuText, App::UI_TEXT);
    DrawHwTile(1, 0, L"\uE7F4", L"Видеокарта", App::gpuName, App::UI_TEXT);

    swprintf(buf, 512, L"%.1f ГБ всего  /  занято %d%%", App::hwInfo.ramTotalGB, App::hwInfo.ramLoadPct);
    DrawHwTile(0, 1, L"\uE964", L"Оперативная память", buf, App::hwInfo.ramLoadPct > 80 ? App::YELLOW : App::UI_TEXT);

    swprintf(buf, 512, L"%ls, свободно %.1f ГБ",
             App::hwInfo.diskIsSSD ? L"SSD" : L"HDD / тип не определён", App::hwInfo.diskFreeGB);
    DrawHwTile(1, 1, L"\uEDA2", L"Диск C:", buf, App::UI_TEXT);

    std::wstring buildText = App::hwInfo.winBuild > 0
        ? L"Windows, build " + std::to_wstring(App::hwInfo.winBuild)
        : std::wstring(L"не определён");
    DrawHwTile(0, 2, L"\uE770", L"Операционная система", buildText, App::UI_TEXT);

    DrawHwTile(1, 2, L"\uE945", L"Питание",
               App::isLaptop ? (App::hwInfo.onACPower ? L"Сеть (ноутбук)" : L"Батарея (ноутбук)")
                             : L"Сеть (стационарный ПК)",
               App::hwInfo.onACPower ? App::UI_GREEN : App::YELLOW);

    DrawHwTile(0, 3, L"\uE72E", L"VBS / изоляция ядра",
               App::hwInfo.vbsEnabled ? L"Включена" : L"Выключена",
               App::hwInfo.vbsEnabled ? App::UI_GREEN : App::UI_MUTED);
    DrawHwTile(1, 3, L"\uE83D", L"Tamper Protection",
               App::hwInfo.tamperProtection ? L"Включена" : L"Выключена",
               App::hwInfo.tamperProtection ? App::UI_GREEN : App::UI_MUTED);

    // Вердикт Guard 2.0
    y += 4 * (tileH + rowGap) + 12;
    int guardH = App::windowH - y - 55;
    if (guardH < 260) guardH = 260;
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)guardH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"ВЕРДИКТ GUARD 2.0", App::fBold, RectF((REAL)(x + 20), (REAL)(y + 14), (REAL)(w - 40), 24), App::UI_TEXT);

    int okCount = 0, warnCount = 0, blockCount = 0;
    std::vector<const App::TweakItem*> okItems, warnItems, blockItems;
    for (const auto& tw : App::tweaksList) {
        bool laptopBlocked = (App::isLaptop && !tw.isLaptopSafe);
        if (!IsTweakAllowedForHardware(tw.id) || laptopBlocked) { blockCount++; blockItems.push_back(&tw); }
        else if (tw.isDangerous) { warnCount++; warnItems.push_back(&tw); }
        else { okCount++; okItems.push_back(&tw); }
    }

    int vColW = (w - 40 - 2 * colGap) / 3;
    int v1X = x + 20, v2X = v1X + vColW + colGap, v3X = v2X + vColW + colGap;
    int vY = y + 48;

    const int kMaxColItems = 5;
    const int itemStep = 20;

    swprintf(buf, 512, L"✅ Доступно: %d", okCount);
    DrawTextGdi(g, buf, App::fBold, RectF((REAL)v1X, (REAL)vY, (REAL)vColW, 22), App::UI_GREEN);
    std::wstring okSub = L"Проверки пройдены";
    if ((int)okItems.size() > kMaxColItems) okSub += L" · ещё " + std::to_wstring((int)okItems.size() - kMaxColItems);
    DrawTextGdi(g, okSub, App::fSmall, RectF((REAL)v1X, (REAL)(vY + 24), (REAL)vColW, 18), App::UI_MUTED);
    for (size_t i = 0; i < okItems.size() && i < (size_t)kMaxColItems; ++i)
        DrawTextEllipsis(g, L"• " + okItems[i]->name, App::fSmall, RectF((REAL)v1X, (REAL)(vY + 46 + (int)i * itemStep), (REAL)vColW, 18), App::UI_TEXT);

    swprintf(buf, 512, L"⚠️ С оговоркой: %d", warnCount);
    DrawTextGdi(g, buf, App::fBold, RectF((REAL)v2X, (REAL)vY, (REAL)vColW, 22), App::YELLOW);
    std::wstring warnSub = L"Опасные — применяйте осознанно";
    if ((int)warnItems.size() > kMaxColItems) warnSub += L" · ещё " + std::to_wstring((int)warnItems.size() - kMaxColItems);
    DrawTextGdi(g, warnSub, App::fSmall, RectF((REAL)v2X, (REAL)(vY + 24), (REAL)vColW, 18), App::UI_MUTED);
    for (size_t i = 0; i < warnItems.size() && i < (size_t)kMaxColItems; ++i)
        DrawTextEllipsis(g, L"• " + warnItems[i]->name, App::fSmall, RectF((REAL)v2X, (REAL)(vY + 46 + (int)i * itemStep), (REAL)vColW, 18), App::UI_TEXT);

    swprintf(buf, 512, L"⛔ Заблокировано: %d", blockCount);
    DrawTextGdi(g, buf, App::fBold, RectF((REAL)v3X, (REAL)vY, (REAL)vColW, 22), App::RED);
    std::wstring blockSub = L"Причины — ниже";
    if ((int)blockItems.size() > kMaxColItems) blockSub += L" · ещё " + std::to_wstring((int)blockItems.size() - kMaxColItems);
    DrawTextGdi(g, blockSub, App::fSmall, RectF((REAL)v3X, (REAL)(vY + 24), (REAL)vColW, 18), App::UI_MUTED);
    for (size_t i = 0; i < blockItems.size() && i < (size_t)kMaxColItems; ++i)
        DrawTextEllipsis(g, L"• " + blockItems[i]->name, App::fSmall, RectF((REAL)v3X, (REAL)(vY + 46 + (int)i * itemStep), (REAL)vColW, 18), App::UI_TEXT);

    int reasonsY = vY + 46 + kMaxColItems * itemStep + 8;
    DrawTextGdi(g, L"ПРИЧИНЫ БЛОКИРОВОК", App::fSmall, RectF((REAL)(x + 20), (REAL)reasonsY, (REAL)(w - 40), 18), App::UI_MUTED);
    reasonsY += 22;

    std::vector<std::pair<std::wstring, int>> reasons;
    for (const auto* tw : blockItems) {
        const wchar_t* r = GetTweakBlockReason(tw->id);
        std::wstring rs;
        if (r) rs = r;
        else if (App::isLaptop && !tw->isLaptopSafe) rs = L"Не рекомендуется для ноутбуков: нагрев и разряд батареи.";
        else continue;
        bool found = false;
        for (auto& p : reasons) {
            if (p.first == rs) { p.second++; found = true; break; }
        }
        if (!found) reasons.push_back({ rs, 1 });
    }

    if (reasons.empty()) {
        DrawTextGdi(g, L"Заблокированных твиков нет — Guard доволен системой.", App::fSmall,
                    RectF((REAL)(x + 20), (REAL)reasonsY, (REAL)(w - 40), 22), App::UI_GREEN);
    } else {
        for (size_t i = 0; i < reasons.size() && i < 4; ++i) {
            std::wstring line = L"• " + reasons[i].first;
            if (reasons[i].second > 1) line += L"  (×" + std::to_wstring(reasons[i].second) + L")";
            DrawTextEllipsis(g, line, App::fSmall,
                             RectF((REAL)(x + 20), (REAL)(reasonsY + (int)i * 24), (REAL)(w - 40), 22),
                             App::RED);
        }
    }
}

// Блок K: человекочитаемый вердикт по железу. Префикс — цвет/иконка для UI.
std::vector<std::wstring> GetSystemVerdict() {
    std::vector<std::wstring> lines;
    bool warning = false, critical = false;

    if (App::hwInfo.ramTotalGB > 0.0 && App::hwInfo.ramTotalGB <= 8.0) {
        lines.push_back(L"[YEL] Мало ОЗУ (8 ГБ). Для игр это вызывает фризы.");
        warning = true;
    }
    if (App::hwInfo.ramLoadPct > 85) {
        lines.push_back(L"[RED] ОЗУ загружено на >85%. Система в файле подкачки.");
        critical = true;
    }
    if (App::hwInfo.diskTypeKnown && !App::hwInfo.diskIsSSD) {
        lines.push_back(L"[RED] Установлен HDD. Замените на SSD.");
        critical = true;
    }
    if (App::hwInfo.diskIsSSD && App::hwInfo.diskFreeGB < 10.0) {
        lines.push_back(L"[YEL] На диске C: менее 10 ГБ. SSD замедляется.");
        warning = true;
    }
    if (App::cpuTemp > 85) {
        lines.push_back(L"[RED] Процессор перегревается! Троттлинг.");
        critical = true;
    }
    if (App::gpuTemp > 85) {
        lines.push_back(L"[RED] Видеокарта перегревается!");
        critical = true;
    }
    if (App::hwInfo.vbsEnabled)
        lines.push_back(L"[INF] Включена изоляция ядра (VBS). Забирает 5-10% FPS.");
    if (App::hwInfo.tamperProtection)
        lines.push_back(L"[INF] Включена защита Tamper Protection.");

    bool tempKnownOk = (App::cpuTemp > 0 && App::cpuTemp < 75) &&
                       (App::gpuTemp <= 0 || App::gpuTemp < 75);
    bool ideal = App::hwInfo.ramTotalGB >= 16.0 &&
                 App::hwInfo.diskTypeKnown && App::hwInfo.diskIsSSD &&
                 App::hwInfo.diskFreeGB > 50.0 && tempKnownOk &&
                 !warning && !critical;
    if (ideal)
        lines.push_back(L"[GRN] Железо отлично сбалансировано!");

    if (lines.empty())
        lines.push_back(L"[INF] Данных для полного вердикта недостаточно. Запустите диагностику.");

    bool hasRed = critical, hasInf = false, hasGrn = false;
    for (const auto& l : lines) {
        if (l.rfind(L"[RED]", 0) == 0) hasRed = true;
        if (l.rfind(L"[INF]", 0) == 0) hasInf = true;
        if (l.rfind(L"[GRN]", 0) == 0) hasGrn = true;
    }
    std::wstring banner;
    if (hasRed)
        banner = L"[BRD] СИСТЕМА ТРЕБУЕТ ВНИМАНИЯ";
    else if ((warning || hasInf) && !hasGrn)
        banner = L"[BYL] ЕСТЬ ОГРАНИЧЕНИЯ";
    else
        banner = L"[BGN] СИСТЕМА В ОТЛИЧНОМ СОСТОЯНИИ";
    lines.insert(lines.begin(), banner);
    return lines;
}

void DrawSensors(Graphics& g) {
    int x = App::SIDE_W + 30, y = 40, w = App::windowW - App::SIDE_W - 60;
    DrawTextGdi(g, L"ДАТЧИКИ И ЖЕЛЕЗО", App::fTitle, RectF((REAL)x, (REAL)y, (REAL)w, 40), App::UI_TEXT);
    DrawTextGdi(g, L"Live-данные системы и полный вердикт по железу", App::fSmall,
                RectF((REAL)(x + 330), (REAL)(y + 12), (REAL)(w - 330), 22), App::UI_MUTED);

    int gap = 20;
    int cardW = (w - gap) / 2;
    int topY = 95, topH = 235;
    int bottomY = topY + topH + 16, bottomH = 140;
    int verdictY = bottomY + bottomH + 16;
    int verdictH = App::windowH - 55 - verdictY;
    if (verdictH < 120) verdictH = 120;

    // ===== CPU =====
    DrawRoundRect(g, RectF((REAL)x, (REAL)topY, (REAL)cardW, (REAL)topH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"ПРОЦЕССОР (CPU)", App::fBold, RectF((REAL)(x + 15), (REAL)(topY + 12), (REAL)(cardW - 30), 22), App::UI_TEXT);
    {
        wchar_t tbuf[40]; Color tc = App::UI_MUTED;
        if (App::cpuTemp > 0) {
            swprintf(tbuf, 40, L"%d°C", App::cpuTemp);
            tc = App::cpuTemp < 70 ? App::UI_GREEN : (App::cpuTemp < 85 ? App::YELLOW : App::RED);
        } else swprintf(tbuf, 40, L"N/A");
        DrawTextGdi(g, tbuf, App::fBold, RectF((REAL)(x + cardW - 110), (REAL)(topY + 12), 95, 22), tc, StringAlignmentFar);
    }
    DrawTextEllipsis(g, App::cpuName, App::fSmall, RectF((REAL)(x + 15), (REAL)(topY + 38), (REAL)(cardW - 30), 18), App::UI_MUTED);
    DrawTextGdi(g, App::cpuText, App::fMetric, RectF((REAL)(x + 15), (REAL)(topY + 60), 120, 40), App::UI_TEXT);
    DrawTextGdi(g, L"Ядра / потоки: " + App::hwInfo.cpuCores, App::fSmall,
                RectF((REAL)(x + 140), (REAL)(topY + 70), (REAL)(cardW - 155), 20), App::UI_GREEN);
    DrawTextGdi(g, L"История загрузки (30 с)", App::fSmall,
                RectF((REAL)(x + 15), (REAL)(topY + 108), (REAL)(cardW - 30), 18), App::UI_MUTED);
    DrawCpuGraph(g, x + 15, topY + 132, cardW - 30, 72);

    // ===== GPU =====
    int x2 = x + cardW + gap;
    DrawRoundRect(g, RectF((REAL)x2, (REAL)topY, (REAL)cardW, (REAL)topH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"ВИДЕОКАРТА (GPU)", App::fBold, RectF((REAL)(x2 + 15), (REAL)(topY + 12), (REAL)(cardW - 30), 22), App::UI_TEXT);
    {
        wchar_t tbuf[40]; Color tc = App::UI_MUTED;
        if (App::gpuTemp > 0) {
            swprintf(tbuf, 40, L"%d°C", App::gpuTemp);
            tc = App::gpuTemp < 75 ? App::UI_GREEN : (App::gpuTemp < 90 ? App::YELLOW : App::RED);
        } else swprintf(tbuf, 40, L"N/A");
        DrawTextGdi(g, tbuf, App::fBold, RectF((REAL)(x2 + cardW - 110), (REAL)(topY + 12), 95, 22), tc, StringAlignmentFar);
    }
    DrawTextEllipsis(g, App::gpuName, App::fSmall, RectF((REAL)(x2 + 15), (REAL)(topY + 38), (REAL)(cardW - 30), 18), App::UI_MUTED);
    if (App::gpuLoad >= 0) {
        wchar_t lbuf[32]; swprintf(lbuf, 32, L"%d%%", App::gpuLoad);
        DrawTextGdi(g, lbuf, App::fMetric, RectF((REAL)(x2 + 15), (REAL)(topY + 62), 150, 40), App::UI_TEXT);
        DrawProgressBar(g, x2 + 15, topY + 110, cardW - 30, 8, App::gpuLoad / 100.0, App::UI_GREEN);
    } else {
        DrawTextGdi(g, L"N/A", App::fMetric, RectF((REAL)(x2 + 15), (REAL)(topY + 62), 150, 40), App::UI_MUTED);
        DrawTextGdi(g, L"Счётчик GPU недоступен", App::fSmall,
                    RectF((REAL)(x2 + 15), (REAL)(topY + 108), (REAL)(cardW - 30), 18), App::UI_MUTED);
    }
    {
        wchar_t vbuf[96];
        if (App::gpuVramTotal > 0.01)
            swprintf(vbuf, 96, L"VRAM: %.1f / %.1f ГБ", App::gpuVramUsed, App::gpuVramTotal);
        else
            swprintf(vbuf, 96, L"VRAM: N/A");
        DrawTextGdi(g, vbuf, App::fBold, RectF((REAL)(x2 + 15), (REAL)(topY + 136), (REAL)(cardW - 30), 22), App::UI_TEXT);
    }
    DrawTextGdi(g, App::gpuTemp > 0 ? L"Температура доступна" : L"Температура: датчик недоступен (NVIDIA: nvidia-smi)",
                App::fSmall, RectF((REAL)(x2 + 15), (REAL)(topY + 170), (REAL)(cardW - 30), 18), App::UI_MUTED);
    DrawTextGdi(g, L"Загрузка и VRAM обновляются каждую секунду", App::fSmall,
                RectF((REAL)(x2 + 15), (REAL)(topY + 196), (REAL)(cardW - 30), 18), App::UI_ACCENT);

    // ===== RAM =====
    DrawRoundRect(g, RectF((REAL)x, (REAL)bottomY, (REAL)cardW, (REAL)bottomH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"ОПЕРАТИВНАЯ ПАМЯТЬ", App::fBold, RectF((REAL)(x + 15), (REAL)(bottomY + 12), (REAL)(cardW - 30), 22), App::UI_TEXT);
    {
        wchar_t rbuf[32]; swprintf(rbuf, 32, L"%d%%", App::ramLoad);
        DrawTextGdi(g, rbuf, App::fMetric, RectF((REAL)(x + 15), (REAL)(bottomY + 40), 120, 40), App::UI_TEXT);
    }
    DrawTextGdi(g, App::ramText, App::fBold, RectF((REAL)(x + 145), (REAL)(bottomY + 50), (REAL)(cardW - 160), 22), App::UI_TEXT);
    DrawProgressBar(g, x + 15, bottomY + 94, cardW - 30, 8, App::ramLoad / 100.0,
                    App::ramLoad > 85 ? App::RED : (App::ramLoad > 70 ? App::YELLOW : App::UI_GREEN));
    DrawTextGdi(g, L"Физическая память (без файла подкачки)", App::fSmall,
                RectF((REAL)(x + 15), (REAL)(bottomY + 112), (REAL)(cardW - 30), 18), App::UI_MUTED);

    // ===== DISK =====
    DrawRoundRect(g, RectF((REAL)x2, (REAL)bottomY, (REAL)cardW, (REAL)bottomH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"ДИСК C:", App::fBold, RectF((REAL)(x2 + 15), (REAL)(bottomY + 12), (REAL)(cardW - 30), 22), App::UI_TEXT);
    std::wstring diskType = !App::hwInfo.diskTypeKnown ? L"Тип не определён" :
                            (App::hwInfo.diskIsSSD ? L"SSD" : L"HDD");
    Color diskColor = !App::hwInfo.diskTypeKnown ? App::YELLOW :
                      (App::hwInfo.diskIsSSD ? App::UI_GREEN : App::RED);
    DrawTextGdi(g, diskType, App::fTitle, RectF((REAL)(x2 + 15), (REAL)(bottomY + 38), (REAL)(cardW - 30), 32), diskColor);

    double diskTotalGB = 0.0, diskFreeGB = App::hwInfo.diskFreeGB;
    ULARGE_INTEGER fb, tb, tf;
    if (GetDiskFreeSpaceExW(L"C:\\", &fb, &tb, &tf)) {
        diskTotalGB = (double)tb.QuadPart / (1024.0 * 1024.0 * 1024.0);
        diskFreeGB = (double)tf.QuadPart / (1024.0 * 1024.0 * 1024.0);
    }
    wchar_t dbuf[128];
    swprintf(dbuf, 128, L"Свободно: %.1f ГБ из %.1f ГБ", diskFreeGB, diskTotalGB);
    DrawTextGdi(g, dbuf, App::fSmall, RectF((REAL)(x2 + 15), (REAL)(bottomY + 74), (REAL)(cardW - 30), 18), App::UI_TEXT);
    double usedFrac = diskTotalGB > 0.0 ? (diskTotalGB - diskFreeGB) / diskTotalGB : 0.0;
    DrawProgressBar(g, x2 + 15, bottomY + 98, cardW - 30, 8, usedFrac,
                    usedFrac > 0.92 ? App::RED : (usedFrac > 0.80 ? App::YELLOW : App::UI_GREEN));
    DrawTextGdi(g, L"Заполненность системного диска", App::fSmall,
                RectF((REAL)(x2 + 15), (REAL)(bottomY + 116), (REAL)(cardW - 30), 18), App::UI_MUTED);

    // ===== Вердикт =====
    DrawRoundRect(g, RectF((REAL)x, (REAL)verdictY, (REAL)w, (REAL)verdictH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"ВЕРДИКТ СИСТЕМЫ", App::fBold, RectF((REAL)(x + 20), (REAL)(verdictY + 12), (REAL)(w - 40), 24), App::UI_TEXT);

    auto verdict = GetSystemVerdict();
    int lineY = verdictY + 42;

    if (!verdict.empty()) {
        std::wstring tag = verdict[0].substr(0, 5);
        std::wstring text = verdict[0].size() > 6 ? verdict[0].substr(6) : verdict[0];
        Color color = App::UI_ACCENT;
        Color fill = App::UI_ACCENT_DIM;
        const wchar_t* icon = L"\u25CF";
        if (tag == L"[BRD]") { color = App::RED;      fill = Color(52, 220, 80, 80); }
        else if (tag == L"[BYL]") { color = App::YELLOW;   fill = Color(52, 240, 200, 80); }
        else if (tag == L"[BGN]") { color = App::UI_GREEN; fill = App::UI_GREEN_DIM; }

        DrawRoundRect(g, RectF((REAL)(x + 20), (REAL)lineY, (REAL)(w - 40), 34), 8.0f, fill, color);
        DrawTextGdi(g, icon, App::fBold, RectF((REAL)(x + 34), (REAL)lineY, 26, 34),
                    color, StringAlignmentCenter, StringAlignmentCenter);
        DrawTextGdi(g, text, App::fBold, RectF((REAL)(x + 68), (REAL)lineY, (REAL)(w - 136), 34),
                    color, StringAlignmentCenter, StringAlignmentCenter);
        lineY += 40;
    }

    int maxLines = std::max(0, (verdictY + verdictH - lineY - 8) / 24);
    for (size_t i = 1; i < verdict.size() && (int)(i - 1) < maxLines; ++i) {
        std::wstring tag = verdict[i].substr(0, 5);
        std::wstring text = verdict[i].size() > 6 ? verdict[i].substr(6) : verdict[i];
        const wchar_t* icon = L"\u2139\uFE0F";
        Color color = App::UI_ACCENT;
        if (tag == L"[RED]") { icon = L"\u274C"; color = App::RED; }
        else if (tag == L"[YEL]") { icon = L"\u26A0\uFE0F"; color = App::YELLOW; }
        else if (tag == L"[GRN]") { icon = L"\u2705"; color = App::UI_GREEN; }
        DrawTextGdi(g, icon, App::fBold, RectF((REAL)(x + 20), (REAL)lineY, 26, 22), color);
        DrawTextGdi(g, text, App::fSmall, RectF((REAL)(x + 52), (REAL)lineY, (REAL)(w - 72), 22), color);
        lineY += 24;
    }
}

// MODULE C — UI страницы «Бенчмарк» (App::BenchmarkPage)
void DrawBenchmark(Graphics& g) {
    int x = App::SIDE_W + 30, y = 36, w = App::windowW - App::SIDE_W - 60;
    bool running = Benchmark::IsRunning();
    Benchmark::Stats st = Benchmark::GetLastStats();
    auto samples = Benchmark::GetLastSamples();

    DrawTextGdi(g, L"БЕНЧМАРК FPS / FRAMETIME (ETW)", App::fTitle, RectF((REAL)x, (REAL)y, (REAL)(w - 380), 36), App::UI_TEXT);
    DrawTextGdi(g, L"Реальный захват Present (DXGI / D3D9 / DxgKrnl) без инжекции в игру — безопасно для анти-читов",
                App::fSmall, RectF((REAL)x, (REAL)(y + 36), (REAL)(w - 380), 20), App::UI_MUTED);

    // Кнопки управления справа сверху: 2 ряда по 2 кнопки
    int btnW = 175, btnH = 40, btnGap = 12;
    int btnExportX = x + w - btnW;
    int btnStartX = btnExportX - btnW - btnGap;
    int btnRow2Y = y + 6 + btnH + 8;

    DrawRoundRect(g, RectF((REAL)btnStartX, (REAL)(y + 6), (REAL)btnW, (REAL)btnH), 10.0f,
                  running ? App::RED : App::UI_ACCENT, running ? App::RED : App::UI_ACCENT);
    DrawTextGdi(g, running ? L"ОСТАНОВИТЬ ЗАМЕР" : L"НАЧАТЬ ЗАМЕР", App::fBodyBold,
                RectF((REAL)btnStartX, (REAL)(y + 6), (REAL)btnW, (REAL)btnH),
                Color(255, 255, 255, 255), StringAlignmentCenter, StringAlignmentCenter);
    AddHit(btnStartX, y + 6, btnW, btnH, running ? 3501 : 3500);

    bool canExport = (st.sampleCount > 0);
    DrawRoundRect(g, RectF((REAL)btnExportX, (REAL)(y + 6), (REAL)btnW, (REAL)btnH), 10.0f,
                  App::UI_CARD, canExport ? App::UI_GREEN : App::UI_BORDER);
    DrawTextGdi(g, L"ЭКСПОРТ PNG", App::fBodyBold,
                RectF((REAL)btnExportX, (REAL)(y + 6), (REAL)btnW, (REAL)btnH),
                canExport ? App::UI_GREEN : App::UI_MUTED, StringAlignmentCenter, StringAlignmentCenter);
    if (canExport) AddHit(btnExportX, y + 6, btnW, btnH, 3510);

    // Сброс сэмплов на лету — ETW-сессия продолжает работать (ID 3511)
    DrawRoundRect(g, RectF((REAL)btnStartX, (REAL)btnRow2Y, (REAL)btnW, (REAL)btnH), 10.0f,
                  App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"СБРОС ЗАМЕРА", App::fBodyBold,
                RectF((REAL)btnStartX, (REAL)btnRow2Y, (REAL)btnW, (REAL)btnH),
                running ? App::UI_TEXT : App::UI_MUTED, StringAlignmentCenter, StringAlignmentCenter);
    if (running) AddHit(btnStartX, btnRow2Y, btnW, btnH, 3511);

    // CSV-экспорт метрик и покадровых данных (ID 3512)
    DrawRoundRect(g, RectF((REAL)btnExportX, (REAL)btnRow2Y, (REAL)btnW, (REAL)btnH), 10.0f,
                  App::UI_CARD, canExport ? App::UI_ACCENT : App::UI_BORDER);
    DrawTextGdi(g, L"ЭКСПОРТ CSV", App::fBodyBold,
                RectF((REAL)btnExportX, (REAL)btnRow2Y, (REAL)btnW, (REAL)btnH),
                canExport ? App::UI_ACCENT : App::UI_MUTED, StringAlignmentCenter, StringAlignmentCenter);
    if (canExport) AddHit(btnExportX, btnRow2Y, btnW, btnH, 3512);

    y += 6 + btnH + 8 + btnH + 12;

    // Статус-бар замера + прогресс на 5 минут
    const uint32_t maxSec = Benchmark::MaxDurationSec();
    const bool autoFinished = !running && st.autoStopped;
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, 64), 10.0f, App::UI_CARD,
                  running ? (st.waitingForGame ? App::YELLOW : App::UI_GREEN)
                          : (autoFinished ? App::YELLOW : App::UI_BORDER));
    wchar_t statusBuf[512];
    if (running && st.waitingForGame) {
        swprintf(statusBuf, 512, L"🟡 Ожидание запуска игры (CS2 / Dota 2 / GTA)...   |   Кадров: 0   |   %u / %u с   |   Профиль: %ls",
                 (unsigned int)st.durationSec, (unsigned int)maxSec,
                 st.notes.empty() ? L"штатный" : st.notes.c_str());
    } else if (autoFinished && st.sampleCount == 0) {
        swprintf(statusBuf, 512, L"🟡 Замер остановлен автоматически: лимит %u минут.   |   Кадров не было — игра не запущена",
                 (unsigned int)(maxSec / 60));
    } else if (autoFinished) {
        swprintf(statusBuf, 512, L"🟡 Замер остановлен автоматически: лимит %u минут.   |   Игра: %ls   |   Кадров: %u   |   Итоги записаны в журнал",
                 (unsigned int)(maxSec / 60), st.gameExe.empty() ? L"(не обнаружена)" : st.gameExe.c_str(),
                 (unsigned int)st.sampleCount);
    } else {
        swprintf(statusBuf, 512, L"%ls   |   Игра: %ls   |   Кадров: %u   |   Длительность: %u / %u с   |   Профиль: %ls",
                 running ? L"🟢 Запись ETW активна" : L"⚪ Замер не запущен",
                 st.gameExe.empty() ? L"(не обнаружена)" : st.gameExe.c_str(),
                 (unsigned int)st.sampleCount, (unsigned int)st.durationSec, (unsigned int)maxSec,
                 st.notes.empty() ? L"штатный" : st.notes.c_str());
    }
    DrawTextEllipsis(g, statusBuf, App::fBody, RectF((REAL)(x + 16), (REAL)(y + 6), (REAL)(w - 32), 26),
                     autoFinished ? App::YELLOW : (running ? (st.waitingForGame ? App::YELLOW : App::UI_GREEN) : App::UI_TEXT));

    int pbX = x + 16, pbY = y + 42, pbW = w - 32, pbH = 10;
    DrawRoundRect(g, RectF((REAL)pbX, (REAL)pbY, (REAL)pbW, (REAL)pbH), 5.0f, App::UI_SIDEBAR, App::UI_BORDER);
    double progress = (maxSec > 0) ? std::min(1.0, (double)st.durationSec / (double)maxSec) : 0.0;
    if (progress > 0.0) {
        REAL fillW = (REAL)std::max(4.0, (double)pbW * progress);
        Color barCol = autoFinished ? App::YELLOW : (running ? App::UI_GREEN : App::UI_ACCENT);
        DrawRoundRect(g, RectF((REAL)pbX, (REAL)pbY, fillW, (REAL)pbH), 5.0f, barCol, barCol);
    }

    y += 78;

    // 4 карточки метрик в ряд: Avg FPS, 1% Low, 0.1% Low, P99 Frametime (мс)
    int gap = 16;
    int cardW = (w - gap * 3) / 4;
    int cardH = 112;
    auto drawCard = [&](int idx, const wchar_t* title, const wchar_t* sub, double val, const wchar_t* unit, Color col) {
        int cx = x + idx * (cardW + gap);
        DrawRoundRect(g, RectF((REAL)cx, (REAL)y, (REAL)cardW, (REAL)cardH), 12.0f, App::UI_CARD, App::UI_BORDER);
        DrawTextGdi(g, title, App::fBodyBold, RectF((REAL)(cx + 16), (REAL)(y + 12), (REAL)(cardW - 32), 20), App::UI_TEXT);
        DrawTextEllipsis(g, sub, App::fSmall, RectF((REAL)(cx + 16), (REAL)(y + 32), (REAL)(cardW - 32), 16), App::UI_MUTED);
        wchar_t vbuf[64];
        swprintf(vbuf, 64, L"%.1f %ls", val, unit);
        DrawTextGdi(g, vbuf, App::fMetric, RectF((REAL)(cx + 16), (REAL)(y + 56), (REAL)(cardW - 32), 42), col);
    };
    drawCard(0, L"Средний FPS", L"Mean Framerate", st.meanFps, L"FPS", App::UI_GREEN);
    drawCard(1, L"1% Low FPS", L"99-й перцентиль", st.low1PctFps, L"FPS", App::UI_ACCENT);
    drawCard(2, L"0.1% Low FPS", L"99.9-й перцентиль (статтеры)", st.low01PctFps, L"FPS", App::YELLOW);
    drawCard(3, L"P99 Frametime", L"Время кадра 99%", st.frametimeP99Us / 1000.0, L"мс", App::UI_TEXT);

    y += cardH + 18;

    // Живой график frametime (последние 600 кадров, шкала 0..50 мс)
    int graphCardH = App::windowH - y - 56;
    if (graphCardH < 180) graphCardH = 180;
    DrawRoundRect(g, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)graphCardH), 12.0f, App::UI_CARD, App::UI_BORDER);
    DrawTextGdi(g, L"ГРАФИК FRAMETIME (ПОСЛЕДНИЕ 600 КАДРОВ, 0–50 МС)", App::fBold,
                RectF((REAL)(x + 18), (REAL)(y + 12), (REAL)(w - 36), 22), App::UI_TEXT);

    int gx = x + 24, gy = y + 42, gw = w - 48, gh = graphCardH - 58;
    const double maxMs = 50.0;
    const double guidesMs[] = { 6.9, 16.6, 33.3 };
    const wchar_t* guideLabels[] = { L"6.9 мс (144 Гц)", L"16.6 мс (60 Гц)", L"33.3 мс (30 Гц)" };
    Pen gridPen(Color(40, 255, 255, 255), 1.0f);
    for (int i = 0; i < 3; ++i) {
        float ly = (float)(gy + gh - (guidesMs[i] / maxMs) * gh);
        g.DrawLine(&gridPen, (REAL)gx, ly, (REAL)(gx + gw), ly);
        DrawTextGdi(g, guideLabels[i], App::fSmall, RectF((REAL)(gx + 6), ly - 18.0f, 150.0f, 16.0f), App::UI_MUTED);
    }

    if (samples.size() >= 2) {
        GraphicsPath path;
        uint32_t worstUs = 0;
        float worstX = 0.0f, worstY = 0.0f;
        float prevX = 0.0f, prevY = 0.0f;
        for (size_t i = 0; i < samples.size(); ++i) {
            double ms = std::min(maxMs, samples[i].frametimeUs / 1000.0);
            float px = (float)gx + (float)i * ((float)gw / (float)(samples.size() - 1));
            float py = (float)(gy + gh - (ms / maxMs) * gh);
            if (i == 0) path.StartFigure();
            else path.AddLine(prevX, prevY, px, py);
            prevX = px; prevY = py;
            if (samples[i].frametimeUs > worstUs) {
                worstUs = samples[i].frametimeUs;
                worstX = px; worstY = py;
            }
        }
        Pen linePen(App::UI_GREEN, 2.0f);
        g.DrawPath(&linePen, &path);

        if (worstUs > 0) {
            SolidBrush spikeBrush(App::RED);
            g.FillEllipse(&spikeBrush, worstX - 4.5f, worstY - 4.5f, 9.0f, 9.0f);
        }
    } else if (running && st.waitingForGame) {
        DrawTextGdi(g, L"Ожидание запуска игры (CS2 / Dota 2 / GTA)... Запустите игру — захват Present начнётся автоматически.",
                    App::fBody, RectF((REAL)gx, (REAL)gy, (REAL)gw, (REAL)gh),
                    App::YELLOW, StringAlignmentCenter, StringAlignmentCenter);
    } else {
        DrawTextGdi(g, L"Нажмите «НАЧАТЬ ЗАМЕР» и разверните игру (CS2, Dota 2, GTA/SAMP и др.) для записи реального фреймтайма.",
                    App::fBody, RectF((REAL)gx, (REAL)gy, (REAL)gw, (REAL)gh),
                    App::UI_MUTED, StringAlignmentCenter, StringAlignmentCenter);
    }
}

void DrawWhole(HDC hdc) {
    Graphics g(hdc);
    const REAL uiScale = (REAL)g_dpi / 96.0f;
    g.ScaleTransform(uiScale, uiScale);
    g.SetSmoothingMode(SmoothingModeAntiAlias); 
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    // +2 логических px перекрывают возможную погрешность округления
    // physical -> logical -> physical по правому/нижнему краю.
    SolidBrush bgBrush(App::BG); g.FillRectangle(&bgBrush, 0, 0, App::windowW + 2, App::windowH + 2);
    App::hits.clear();
    DrawSidebar(g);
    
    if (App::page == App::Dashboard) DrawDashboard(g);
    else if (App::page == App::Tweaks) DrawTweaks(g);
    else if (App::page == App::Games) DrawGames(g);
    else if (App::page == App::Cleaner) DrawCleaner(g);
    else if (App::page == App::Restore) DrawRestore(g);
    else if (App::page == App::Diagnostics) DrawDiagnostics(g);
    else if (App::page == App::Sensors) DrawSensors(g);
    else if (App::page == App::BenchmarkPage) DrawBenchmark(g);

    int btnW = 46, btnH = 32;
    int startX = App::windowW - btnW * 3;
    int startY = 0;
    SolidBrush btnBg(App::UI_SIDEBAR); 
    g.FillRectangle(&btnBg, startX, startY, btnW * 3, btnH);
    
    DrawTextGdi(g, L"\uE8BB", App::fIcon, RectF((REAL)(startX + btnW*2), (REAL)startY, (REAL)btnW, (REAL)btnH), App::RED, StringAlignmentCenter, StringAlignmentCenter); 
    AddHit(startX + btnW*2, startY, btnW, btnH, 9003);
    
    DrawTextGdi(g, L"\uE922", App::fIcon, RectF((REAL)(startX + btnW), (REAL)startY, (REAL)btnW, (REAL)btnH), App::UI_TEXT, StringAlignmentCenter, StringAlignmentCenter); 
    AddHit(startX + btnW, startY, btnW, btnH, 9002);
    
    DrawTextGdi(g, L"\uE921", App::fIcon, RectF((REAL)startX, (REAL)startY, (REAL)btnW, (REAL)btnH), App::UI_TEXT, StringAlignmentCenter, StringAlignmentCenter); 
    AddHit(startX, startY, btnW, btnH, 9001);

    DrawBottomBar(g); 
}

void DispatchAction(int id) {
    if (id == 9001) { ShowWindow(App::hwnd, SW_MINIMIZE); return; } // Свернуть
    if (id == 9002) { ShowWindow(App::hwnd, IsZoomed(App::hwnd) ? SW_RESTORE : SW_MAXIMIZE); return; } // Развернуть
    if (id == 9003) { PostMessageW(App::hwnd, WM_CLOSE, 0, 0); return; } // Закрыть

    {
        HANDLE hfdbg = CreateFileW(App::debugLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hfdbg != INVALID_HANDLE_VALUE) {
            char b[128];
            int l = sprintf_s(b, "DispatchAction id=%d admin=%d\r\n", id, (int)IsUserAnAdmin());
            DWORD w; WriteFile(hfdbg, b, l, &w, NULL); CloseHandle(hfdbg);
        }
    }

    if ((id >= 3000 && id <= 3102) || (id >= 4000 && id < 4600) || (id >= 6000 && id < 6500)) {
        if (!IsUserAnAdmin() && id != 3100) {
            MessageBoxW(App::hwnd, L"Для применения этих настроек перезапустите программу от имени Администратора!", L"Нет прав", MB_OK);
            return;
        }
    }

    if (id >= 100 && id <= 107) {
        App::page = (App::Page)(id - 100);
        if (App::page == App::Restore) {
            App::cachedGroups = Monitor::GetGroupsSnapshot();
        }
        App::tweaksScrollY = 0;
        App::gamesScrollY = 0;
        InvalidateRect(App::hwnd, nullptr, FALSE);
        return;
    }
    if (id >= 2000 && id < 2000 + App::CAT_COUNT) { App::activeCategory = (App::Category)(id - 2000); App::tweaksScrollY = 0; InvalidateRect(App::hwnd, nullptr, FALSE); return; }
    if (id == 2100) {
        App::showPlaceboTweaks = !App::showPlaceboTweaks;
        App::tweaksScrollY = 0;
        InvalidateRect(App::hwnd, nullptr, FALSE);
        return;
    }
    
    if (id >= 3301 && id <= 3304) {
        int idx = id - 3301;
        if (idx == 0) App::cleanTemp = !App::cleanTemp;
        if (idx == 1) App::cleanPrefetch = !App::cleanPrefetch;
        if (idx == 2) App::cleanRecycle = !App::cleanRecycle;
        if (idx == 3) App::cleanLogs = !App::cleanLogs;
        InvalidateRect(App::hwnd, nullptr, FALSE);
        return;
    }

    if (id >= 5000 && id < 5500) {
        int idx = id - 5000;
        if (idx < (int)App::tweaksList.size()) {
            App::selectedTweakIdx = idx;
            InvalidateRect(App::hwnd, nullptr, FALSE);
        }
        return;
    }
    
    if (id >= 6000 && id < 6500) {
        int idx = id - 6000;
        if (idx < (int)App::tweaksList.size()) {
            App::selectedTweakIdx = idx; 
            auto& tw = App::tweaksList[idx];
            
            // Guard ДО изменения состояния. Блокируем только включение: динамические
            // условия (RAM/батарея/VBS) не должны мешать выключить уже применённый твик.
            const wchar_t* blockReason = GetTweakBlockReason(tw.id);
            if (blockReason && !tw.isEnabled) {
                if (IsTweakWarningOnly(tw.id)) {
                    // 2a.1: HVCI — не запрет. Показываем честное предупреждение,
                    // применяем только после явного «Да».
                    if (MessageBoxW(App::hwnd,
                        (std::wstring(blockReason) + L"\n\nВсё равно включить твик?").c_str(),
                        L"🛡️ Hardware Guard — предупреждение",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                        InvalidateRect(App::hwnd, nullptr, FALSE);
                        return;
                    }
                } else {
                    MessageBoxW(App::hwnd, blockReason, L"🛡️ Hardware Guard — твик заблокирован", MB_OK | MB_ICONINFORMATION);
                    InvalidateRect(App::hwnd, nullptr, FALSE);
                    return;
                }
            }
            if (tw.isLaptopSafe == false && App::isLaptop && !tw.isEnabled) {
                // Пакет #9: предупреждение больше не inert — «Нет» отменяет применение.
                if (MessageBoxW(App::hwnd,
                    L"Этот твик не рекомендуется для ноутбуков (нагрев/разряд батареи).\n\nВсё равно применить?",
                    L"Внимание", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                    InvalidateRect(App::hwnd, nullptr, FALSE);
                    return;
                }
            }
            {
                // Guard против гонки фоновых команд (1120 / 1122) ДО сброса tw.isEnabled
                if (tw.isEnabled && (tw.id == 1120 || tw.id == 1122) && App::busyJob.load(std::memory_order_relaxed)) {
                    MessageBoxW(App::hwnd, L"Дождитесь завершения операции.", L"LEFTIK", MB_OK);
                    InvalidateRect(App::hwnd, nullptr, FALSE);
                    return;
                }

                // Список твиков, которые могут задеть анти-чит
                static const int kAntiCheatSensitiveTweaks[] = {
                    1002,   // Win32Priority
                    1097,   // MSI Mode
                    1099,   // CSRSS High Priority
                    1104,   // Kernel Memory Lock
                    1017,   // VBS off
                    1133,   // Dynamic tick
                };

                if (!tw.isEnabled) {
                    std::wstring acName;
                    if (IsAntiCheatActive(&acName)) {
                        bool isRisky = false;
                        for (int rid : kAntiCheatSensitiveTweaks)
                            if (tw.id == rid) { isRisky = true; break; }

                        if (isRisky) {
                            if (MessageBoxW(App::hwnd,
                                (L"⚠️ АНТИ-ЧИТ АКТИВЕН: " + acName + L"\n\n"
                                 L"Твик «" + tw.name + L"» изменяет системные приоритеты\n"
                                 L"и может быть расценен анти-читом как подозрительный.\n\n"
                                 L"Всё равно применить?").c_str(),
                                L"LEFTIK — Риск бана", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                                InvalidateRect(App::hwnd, nullptr, FALSE);
                                return;
                            }
                        }
                    }
                }

                tw.isEnabled = !tw.isEnabled;
                SaveTweakState(tw.id, tw.isEnabled);
                
                if (tw.id == 1070) {
                    if (tw.isEnabled) {
                        wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
                        std::wstring autostart = L"\"" + std::wstring(path) + L"\" --silent";
                        if (!Journal::SetString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                                L"LEFTIK_Optimizer", autostart, L"Автозагрузка Windows")) {
                            tw.isEnabled = false;
                            SaveTweakState(1070, false);
                        }
                    } else {
                        RevertTweak(1070);
                    }
                } 
                else if (tw.isEnabled) {
                    switch(tw.id) {
                        case 1000: T_GameDVR(); break; case 1001: T_MouseAccel(); break; case 1002: T_Priority(); break; case 1003: T_CrossDevice(); break;
                        case 1004: T_SvcHost(); break; case 1005: T_NTFS8_3(); break; case 1006: T_NTFSLast(); break; case 1007: T_AutoMaint(); break;
                        case 1008: T_MouseBuffer(); break;
                        case 1010: T_Defender(); break; case 1011: T_UAC(); break; case 1012: T_SecCenter(); break; case 1013: T_SmartScreen(); break;
                        case 1014: T_Firewall(); break; case 1015: T_AMSI(); break; case 1016: T_CodeInteg(); break; case 1017: T_VBS(); break;
                        case 1018: T_HVCI(); break;
                        case 1020: T_Ultimate(); break; case 1021: T_PowerThrot(); break; case 1023: T_Hibernate(); break; case 1024: T_USBSuspend(); break;
                        case 1030: T_QoS(); break; case 1031: T_IPv6(); break; case 1032: T_Nagle(); break; case 1033: T_RSS(); break;
                        case 1034: T_Checksum(); break; case 1035: T_LSO(); break;
                        case 1040: T_SysMain(); break; case 1041: T_WSearch(); break; case 1042: T_OneDrive(); break; case 1043: T_Xbox(); break; case 1044: T_Edge(); break; case 1045: T_Teams(); break;
                        case 1050: T_Telemetry(); break; case 1051: T_AdID(); break; case 1052: T_Location(); break;
                        case 1060: T_Spooler(); break; case 1061: T_RemoteReg(); break; case 1062: T_Fax(); break; case 1063: T_WerSvc(); break; case 1064: T_Bluetooth(); break;
                        case 1065: T_TabletInputService(); break;
                        case 2001: F_WU(); break;
                        case 2002: F_Store(); break;
                        case 2003:
                            if (!F_Net()) {
                                tw.isEnabled = false;
                                SaveTweakState(2003, false);
                            }
                            break;
                        case 2004: F_Icons(); break;
                        case 2006:
                            if (!F_Dism()) {
                                tw.isEnabled = false;
                                SaveTweakState(2006, false);
                            }
                            break;
                        case 2007: F_EAC(); break; case 2008: F_Vanguard(); break; case 2009: F_BlueStacks(); break; case 2010: F_BluetoothAudio(); break;
                        // ВНИМАНИЕ: ID взяты из утвержденного списка задания (2012 CompactOS, 2013 Discord, 2014 Audio).
                        case 2011:
                            if (!F_StoreFix()) {
                                tw.isEnabled = false;
                                SaveTweakState(2011, false);
                            }
                            break;
                        case 2012:
                            // Пользователь может отказаться от подтверждения — тумблер возвращаем назад.
                            if (!F_CompactOS()) {
                                tw.isEnabled = false;
                                SaveTweakState(2012, false);
                            }
                            break;
                        case 2013: F_DiscordFix(); break;
                        case 2014:
                            if (!F_AudioFix()) {
                                tw.isEnabled = false;
                                SaveTweakState(2014, false);
                            }
                            break;
                        case 2015:
                            if (!F_FixPrintSpooler()) {
                                tw.isEnabled = false;
                                SaveTweakState(2015, false);
                            }
                            break;
                        case 2016: F_FixXboxServices(); break;
                        case 2017: F_FixWebView2(); break;
                        case 2018: F_FixGta5Crash(); break;
                        case 2019: F_RevertDwmTweaks(); break;
                        case 2020: F_FixFiveMAudio(); break;
                        case 2021: F_FixDiscordShare(); break;
                        case 3010: T_DNS(); break;
                        case 1071: T_MenuShowDelay(); break; case 1072: T_StartupDelay(); break; case 1073: T_PowerThrot(); break;
                        case 1074:
                            if (!T_HwSchMode()) {
                                tw.isEnabled = false;
                                SaveTweakState(1074, false);
                            }
                            break;
                        case 1075: T_Prefetcher(); break; case 1076: T_FSEBehavior(); break;
                        case 1080:
                            if (!TimerMgr::Acquire(L"tweak1080")) {
                                tw.isEnabled = false;
                                SaveTweakState(1080, false);
                                MessageBoxW(App::hwnd, L"Не удалось включить таймер высокого разрешения (0.5ms).", L"LEFTIK", MB_OK | MB_ICONWARNING);
                            }
                            break;
                        case 1081: ClearDXCache(); break;
                        case 1082: T_AdvancedNetwork(); break;
                        case 1094: T_NetworkThrottling(); break; case 1095: T_SystemResponsiveness(); break; case 1096: T_DisableTransparency(); break;
                        case 1097: T_EnableGpuMsiMode(); break; case 1098: T_MmcssGamesProfile(); break; case 1099: T_RawInputBoost(); break;
                        case 1100: T_NvidiaPowerMax(); break; case 1101: T_NvidiaTelemetryOff(); break;
                        case 1102: T_CpuUnparkAndBoost(); break; case 1103: T_DwmMpoFix(); break; case 1104: T_KernelMemoryLock(); break;
                        case 1110: T_AmdUlpsOff(); break; case 1111: T_AmdTelemetryOff(); break;
                        case 1120: T_MemoryCompressionToggle(); break; case 1121: T_DisableWidgets(); break; case 1122: T_TcpAutotuning(); break;
                        case 1130: T_DisableGameMode(); break; case 1131: T_DeferUpdates(); break; case 1132: T_CloudClipboard(); break;
                        case 1133: T_DisableDynamicTick(); break;
                        case 1140: T_SteamLite(); break;
                    }
                    // 1074 (HAGS) и 1133 (DynamicTick) уже показывают подробное окно внутри своих T_* функций.
                    if (tw.isEnabled && tw.requiresReboot && tw.id != 1074 && tw.id != 1133) {
                        MessageBoxW(App::hwnd,
                            L"Этот твик вступает в силу ТОЛЬКО после перезагрузки ПК. До перезагрузки некоторые игры могут работать некорректно (например, FPS может быть ограничен 60).",
                            L"LEFTIK", MB_OK | MB_ICONWARNING);
                    }
                    // Без плашки: состояние видно по тумблеру, интерфейс перерисовывается ниже
                }
                else {
                    RevertTweak(tw.id);
                }
                Monitor::InvalidateGroupsCache();
            }
            InvalidateRect(App::hwnd, nullptr, FALSE);
        }
        return;
    }
    
    switch(id) {
        case 3100: 
            {
                MEMORYSTATUSEX before; before.dwLength = sizeof(before);
                GlobalMemoryStatusEx(&before);
                double beforeUsedGB = (before.ullTotalPhys - before.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);

                TrimBackgroundWorkingSets();
                if (MemoryOptimizer::ClearStandbyList()) {
                    MEMORYSTATUSEX after; after.dwLength = sizeof(after);
                    GlobalMemoryStatusEx(&after);
                    double afterUsedGB = (after.ullTotalPhys - after.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
                    double freedGB = beforeUsedGB - afterUsedGB;
                    wchar_t msg[256];
                    if (freedGB > 0.01) {
                        swprintf(msg, 256, L"ОЗУ очищена.\nБыло занято: %.2f ГБ\nСтало занято: %.2f ГБ\nОсвобождено: %.2f ГБ",
                            beforeUsedGB, afterUsedGB, freedGB);
                    } else {
                        swprintf(msg, 256, L"ОЗУ очищена.\nБыло занято: %.2f ГБ\nСтало занято: %.2f ГБ\n\nСвободной памяти было много, поэтому изменений не видно.",
                            beforeUsedGB, afterUsedGB);
                    }
                    MessageBoxW(App::hwnd, msg, L"LEFTIK", MB_OK);
                } else {
                    MessageBoxW(App::hwnd, L"Очистка ОЗУ не удалась. См. leftik_debug.log", L"Ошибка", MB_OK);
                }
            }
            break;
        case 3000: 
            if (MessageBoxW(App::hwnd,
                L"Будут применены твики (каждый с точным откатом через журнал):\n"
                L"  • GameDVR Off (1000)\n"
                L"  • Win32Priority (1002)\n"
                L"  • QoS Limit (1030)\n\n"
                L"НЕ будут тронуты: SysMain (1040) и Телеметрия (1050) —\n"
                L"они применяются только отдельными тумблерами в разделе «Твики».\n\n"
                L"Также будут закрыты фоновые приложения:\n"
                L"браузеры, мессенджеры (Discord, Telegram), игровые\n"
                L"лаунчеры, облачные хранилища, RGB-софт, фоновые утилиты.\n"
                L"Несохранённые данные в них будут потеряны.\n\n"
                L"Таймер 0.5ms будет включён (владелец: Умная оптимизация),\n"
                L"standby-память будет очищена (разово).\n\nПродолжить?",
                L"Умная оптимизация", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            KillBackgroundApps();
            {
                bool timerOk = TimerMgr::Acquire(L"smart3000");
                {
                    App::activeGroupId = L"smart3000";
                    struct GroupGuard { ~GroupGuard() { App::activeGroupId.clear(); } } grpGuard;
                    T_GameDVR(); T_Priority(); T_QoS();
                }
                Monitor::InvalidateGroupsCache();
                MemoryOptimizer::ClearStandbyList();
                // Честный отчёт: «применено» = подтверждено чтением реестра
                int confirmed3 = (ReadTweakState(1000) ? 1 : 0) + (ReadTweakState(1002) ? 1 : 0) + (ReadTweakState(1030) ? 1 : 0);
                for (auto& t : App::tweaksList) t.isEnabled = ReadTweakState(t.id);
                InvalidateRect(App::hwnd, nullptr, FALSE);

                const wchar_t* timerLine = timerOk
                    ? L"Таймер 0.5ms включён, ОЗУ очищена.\n"
                    : L"ОЗУ очищена, но таймер 0.5ms включить НЕ удалось.\n";
                wchar_t summary[640];
                int totalMatched = App::lastKilledCount + App::lastFailedCount;
                if (App::lastFailedCount > 0) {
                    swprintf(summary, 640,
                        L"Умная оптимизация завершена.\n\n"
                        L"Твиков подтверждено: %d из 3 (1000/1002/1030).\n"
                        L"Приложений закрыто: %d из %d\n"
                        L"Не удалось закрыть: %d (см. leftik_debug.log)\n\n"
                        L"%ls"
                        L"SysMain и Телеметрия не трогались.",
                        confirmed3, App::lastKilledCount, totalMatched, App::lastFailedCount, timerLine);
                } else {
                    swprintf(summary, 640,
                        L"Умная оптимизация завершена.\n\n"
                        L"Твиков подтверждено: %d из 3 (1000/1002/1030).\n"
                        L"Приложений закрыто: %d\n\n"
                        L"%ls"
                        L"SysMain и Телеметрия не трогались.",
                        confirmed3, App::lastKilledCount, timerLine);
                }
                MessageBoxW(App::hwnd, summary, L"LEFTIK", MB_OK);
            }
            break;
        case 3001:
            ApplyMagicBoost();
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3002:
            ApplyTurboBoost();
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3050:
            App::backgroundMode = !App::backgroundMode;
            SaveBackgroundMode(App::backgroundMode);
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3051:
            // v58.0 Feature B: авто-очистка standby.
            if (!App::hwInfo.diskIsSSD) {
                MessageBoxW(App::hwnd,
                    L"Авто-очистка standby доступна только на SSD.\n"
                    L"На HDD это замедляет файловый кэш.",
                    L"LEFTIK", MB_OK | MB_ICONINFORMATION);
                break;
            }
            App::autoStandbyClean = !App::autoStandbyClean;
            SaveStandbySettings();
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3052:
            if (!App::hwInfo.diskIsSSD) break;
            if (App::standbyIntervalMin == 30) App::standbyIntervalMin = 60;
            else if (App::standbyIntervalMin == 60) App::standbyIntervalMin = 120;
            else if (App::standbyIntervalMin == 120) App::standbyIntervalMin = 240;
            else App::standbyIntervalMin = 30;
            SaveStandbySettings();
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3053: {
            // 2a.3: официальный URI Xbox Mode по документации Microsoft —
            // ms-settings:gaming-fullscreen. На старых сборках страницы может не быть:
            // при явном отказе ShellExecute открываем общую страницу «Игры».
            // LEFTIK ничего не пишет в реестр и не дублирует режим.
            HINSTANCE r = ShellExecuteW(App::hwnd, L"open", L"ms-settings:gaming-fullscreen",
                                        nullptr, nullptr, SW_SHOWNORMAL);
            if ((INT_PTR)r <= 32) {
                ShellExecuteW(App::hwnd, L"open", L"ms-settings:gaming",
                              nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        }
        case 3101: {
            // Безопасный игровой профиль v57.13: НЕ трогает Defender/UAC/Firewall/VBS и пр.,
            // НЕ закрывает приложения автоматически, включает только таймер (под учетом владельца).
            bool enabling = !App::isMaxActive;
            if (enabling && (App::isUltraActive || App::turboBoostActive)) {
                MessageBoxW(App::hwnd, L"Сначала выключите «Ультра оптимизацию» или ТУРБО-РЕЖИМ — одновременные профили не поддерживаются.", L"LEFTIK", MB_OK);
                break;
            }
            if (enabling) {
                if (MessageBoxW(App::hwnd,
                    L"Профиль «PRO Высокий»:\n"
                    L"  • Таймер 0.5ms (вернётся при выключении профиля)\n"
                    L"  • Разовая очистка standby-памяти\n\n"
                    L"Defender, UAC, брандмауэр и другие защитные механизмы\n"
                    L"НЕ изменяются. Приложения НЕ закрываются.\n\nПродолжить?",
                    L"PRO Высокий", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
                if (TimerMgr::Acquire(L"profile-pro")) {
                    MemoryOptimizer::ClearStandbyList(); // разовое действие; эффект не гарантируется
                    App::isMaxActive = true;
                    MessageBoxW(App::hwnd, L"Профиль PRO включён: таймер 0.5ms, standby очищена.\nЗащитные механизмы не изменялись.", L"LEFTIK", MB_OK);
                } else {
                    TimerMgr::Release(L"profile-pro");
                    MessageBoxW(App::hwnd, L"Не удалось включить таймер 0.5ms — профиль НЕ активирован.", L"LEFTIK", MB_OK | MB_ICONWARNING);
                }
            } else {
                TimerMgr::Release(L"profile-pro");
                App::isMaxActive = false;
                MessageBoxW(App::hwnd, L"Профиль PRO выключен: таймер возвращён к дефолту.\n(Реестр профилем не менялся — откат не требуется.)", L"LEFTIK", MB_OK);
            }
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        }
        case 3102: {
            // Профиль ULTRA v57.14: таймер + standby + персистентные твики (1000/1002/1074)
            // со СВОИМ snapshot'ом на момент включения. При выключении реестр возвращается
            // к состоянию ДО профиля (не к дефолтам T_*). Защита не отключается.
            bool enabling = !App::isUltraActive;
            if (enabling && (App::isMaxActive || App::turboBoostActive)) {
                MessageBoxW(App::hwnd, L"Сначала выключите «PRO Высокий» или ТУРБО-РЕЖИМ — одновременные профили не поддерживаются.", L"LEFTIK", MB_OK);
                break;
            }
            if (enabling) {
                if (MessageBoxW(App::hwnd,
                    L"Профиль «ULTRA» (безопасная редакция):\n"
                    L"  • Таймер 0.5ms (вернётся при выключении профиля)\n"
                    L"  • Разовая очистка standby-памяти\n"
                    L"  • GameDVR Off (1000), Win32Priority (1002), HAGS (1074)\n"
                    L"    — при выключении профиля будет восстановлено состояние\n"
                    L"    ДО профиля (а не дефолты). HAGS активируется после перезагрузки.\n\n"
                    L"Defender, UAC, брандмауэр, VBS НЕ изменяются.\n"
                    L"Edge/Teams НЕ удаляются. Приложения НЕ закрываются.\n\nПродолжить?",
                    L"ULTRA", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
                if (!TimerMgr::Acquire(L"profile-ultra")) {
                    TimerMgr::Release(L"profile-ultra");
                    MessageBoxW(App::hwnd, L"Не удалось включить таймер 0.5ms — профиль НЕ активирован, реестр не менялся.", L"LEFTIK", MB_OK | MB_ICONWARNING);
                    break;
                }
                // Граница области отката: всё, что журнал получит после этой строки,
                // принадлежит профилю и будет откачено при выключении.
                // Журнал недоступен => профиль без точки отката запрещён (критерий D)
                if (!Journal::WriteMarker(L"profile-ultra")) {
                    TimerMgr::Release(L"profile-ultra");
                    MessageBoxW(App::hwnd, L"Журнал недоступен для записи — профиль НЕ активирован (изменения без точки отката запрещены).", L"LEFTIK", MB_OK | MB_ICONWARNING);
                    break;
                }
                // Каждый T_* применяет значение через SetDwordAutoGroup => BackupGrouped
                // исходных байтов выполняется в момент включения профиля (снапшот «до профиля»)
                {
                    App::activeGroupId = L"profile-ultra";
                    struct GroupGuard { ~GroupGuard() { App::activeGroupId.clear(); } } grpGuard;
                    T_GameDVR(); T_Priority(); T_HwSchMode();
                }
                Monitor::InvalidateGroupsCache();
                MemoryOptimizer::ClearStandbyList(); // разовое действие, эффект не гарантируется
                App::isUltraActive = true; // профиль активен; детали применения — в отчёте ниже
                for (auto& t : App::tweaksList) t.isEnabled = ReadTweakState(t.id);
                bool v1000 = ReadTweakState(1000), v1002 = ReadTweakState(1002), v1074 = ReadTweakState(1074);
                int confirmed = (v1000 ? 1 : 0) + (v1002 ? 1 : 0) + (v1074 ? 1 : 0);
                wchar_t msg[768];
                if (confirmed == 3) {
                    swprintf(msg, 768,
                        L"Профиль ULTRA включён.\n\n"
                        L"Таймер 0.5ms включён, standby очищена.\n"
                        L"Твики подтверждены чтением реестра: 3 из 3.\n"
                        L"HAGS: записан в реестр, активируется после перезагрузки.\n\n"
                        L"Защитные механизмы не изменялись.\n"
                        L"Выключение профиля вернёт реестр к состоянию ДО профиля.");
                } else {
                    std::wstring unconf;
                    if (!v1000) unconf += L"  • GameDVR (1000)\n";
                    if (!v1002) unconf += L"  • Win32Priority (1002)\n";
                    if (!v1074) unconf += L"  • HAGS (1074)\n";
                    swprintf(msg, 768,
                        L"Профиль ULTRA включён С ОГОВОРКАМИ.\n\n"
                        L"Таймер 0.5ms включён, standby очищена.\n"
                        L"Подтверждено чтением реестра: %d из 3.\n"
                        L"Не подтверждено (значение могло не примениться):\n%s\n"
                        L"HAGS, если записан, активируется после перезагрузки.\n"
                        L"Выключение профиля откатит область к состоянию до профиля.",
                        confirmed, unconf.c_str());
                }
                MessageBoxW(App::hwnd, msg, L"LEFTIK", MB_OK);
            } else {
                int r = 0, rf = 0, cr = 0, cf = 0, uns = 0;
                int res = Journal::RollbackToMarker(L"profile-ultra", r, rf, cr, cf, uns);
                Monitor::InvalidateGroupsCache();
                for (auto& t : App::tweaksList) t.isEnabled = ReadTweakState(t.id);
                wchar_t msg[768];
                if (res == 0) {
                    TimerMgr::Release(L"profile-ultra");
                    App::isUltraActive = false;
                    swprintf(msg, 768,
                        L"Профиль ULTRA выключен.\n\n"
                        L"Реестр возвращён к состоянию ДО профиля:\n"
                        L"восстановлено значений: %d, команд: %d.\n"
                        L"Таймер возвращён к дефолту.", r, cr);
                    MessageBoxW(App::hwnd, msg, L"LEFTIK", MB_OK);
                } else if (res == 1) {
                    // Неполный откат: флаг и таймер ОСТАЮТСЯ включёнными — состояние восстановлено не полностью
                    swprintf(msg, 768,
                        L"ОТКАТ ПРОФИЛЯ НЕПОЛНЫЙ — профиль остаётся ВКЛЮЧЁННЫМ.\n\n"
                        L"Восстановлено значений: %d\n"
                        L"Ошибок реестра: %d, ошибок команд: %d, неподдерживаемых записей: %d.\n\n"
                        L"Журнал сохранён (создана копия .failed).\n"
                        L"Устраните причину и повторите выключение профиля,\n"
                        L"либо выполните полное «Восстановление».",
                        r, rf, cf, uns);
                    MessageBoxW(App::hwnd, msg, L"LEFTIK", MB_OK | MB_ICONWARNING);
                } else if (res == 2) {
                    // Маркер не найден, но реестр изменён — флаг профиля и таймер оставляем включёнными
                    MessageBoxW(App::hwnd,
                        L"ВНИМАНИЕ: точка отката профиля в журнале не найдена\n"
                        L"(журнал мог быть удалён или уже откачен).\n\n"
                        L"Профиль и таймер оставлены включёнными. Выполните полное «Восстановление», чтобы сбросить все профили.",
                        L"LEFTIK", MB_OK | MB_ICONWARNING);
                } else {
                    // Реестр откачен, но область журнала переписать не удалось:
                    // флаг и таймер остаются, повторное выключение безопасно (идемпотентно)
                    swprintf(msg, 768,
                        L"Реестр возвращён к состоянию до профиля (восстановлено %d),\n"
                        L"но переписать журнал не удалось.\n"
                        L"Флаг профиля оставлен включённым: повторите выключение.",
                        r);
                    MessageBoxW(App::hwnd, msg, L"LEFTIK", MB_OK | MB_ICONWARNING);
                }
            }
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        }
        case 3400:
            RefreshHardwareFlags();
            DetectDiskType();
            App::g_hardwareTier = DetectHardwareTier();
            MessageBoxW(App::hwnd,
                (std::wstring(L"Сканирование железа завершено.\nКатегория ПК: ") + HardwareTierName(App::g_hardwareTier)).c_str(),
                L"LEFTIK", MB_OK);
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3401:
            MessageBoxW(App::hwnd,
                L"ТОП-5 УТИЛИТ ДЛЯ МАКСИМАЛЬНОГО БУСТА (как на скринах):\n\n"
                L"1. MSI Afterburner + RivaTuner — для разгона видеокарты и мониторинга FPS.\n"
                L"2. ParkControl — для отключения парковки ядер (если не доверяете нашему твику).\n"
                L"3. TimerResolution (или ISLC) — таймер 0.5мс в один клик.\n"
                L"4. RawAccel — продвинутая настройка ускорения мыши (если стандартный 1:1 не подходит).\n"
                L"5. QuickCPU — ручная настройка частот и парковки.\n\n"
                L"⚠️ ВНИМАНИЕ: LEFTIK уже делает 90%% этих настроек через реестр.\n"
                L"Ставьте сторонний софт ТОЛЬКО если хотите тонкий ручной контроль.\n"
                L"Скачивайте только с официальных GitHub-репозиториев!",
                L"LEFTIK — Буст-софт", MB_OK | MB_ICONINFORMATION);
            break;
        case 3500:
            FpsMonitor::Start();
            Benchmark::Start(App::hwnd);
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3501:
            FpsMonitor::Stop();
            // Stop() может ждать ProcessTrace/CloseTrace — уводим в фон,
            // UI получит WM_LEFTIK_BENCH_STOPPED.
            Benchmark::RequestStop(App::hwnd);
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3511:
            Benchmark::ResetSamples();
            InvalidateRect(App::hwnd, nullptr, FALSE);
            break;
        case 3510: {
            SYSTEMTIME st{}; GetLocalTime(&st);
            wchar_t fileBuf[MAX_PATH]{};
            swprintf(fileBuf, MAX_PATH, L"leftik_benchmark_%04d%02d%02d_%02d%02d%02d.png",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            OPENFILENAMEW ofn{ sizeof(ofn) };
            ofn.hwndOwner = App::hwnd;
            ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"png";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&ofn)) {
                if (Benchmark::ExportGraphPng(fileBuf)) {
                    MessageBoxW(App::hwnd, (L"График бенчмарка сохранён:\n" + std::wstring(fileBuf)).c_str(), L"LEFTIK Benchmark", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(App::hwnd, L"Не удалось сохранить PNG.", L"LEFTIK Benchmark", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }
        case 3512: {
            SYSTEMTIME st{}; GetLocalTime(&st);
            wchar_t fileBuf[MAX_PATH]{};
            swprintf(fileBuf, MAX_PATH, L"leftik_benchmark_%04d%02d%02d_%02d%02d%02d.csv",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            OPENFILENAMEW ofn{ sizeof(ofn) };
            ofn.hwndOwner = App::hwnd;
            ofn.lpstrFilter = L"CSV (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"csv";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&ofn)) {
                if (Benchmark::ExportCsv(fileBuf)) {
                    MessageBoxW(App::hwnd, (L"Метрики бенчмарка сохранены в CSV:\n" + std::wstring(fileBuf)).c_str(), L"LEFTIK Benchmark", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(App::hwnd, L"Не удалось сохранить CSV: нет сэмплов или файл занят.", L"LEFTIK Benchmark", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }
        case 3200: RunCleaner(); break;
        case 3201: SmartDiskClean(); break;
        case 3202: CleanEmptyAppTraces(); break;
        case 3203: CleanSampCache(); break;
        case 3305: CreateRestorePoint(); break;
        case 3306: {
            if (App::selectedGroups.empty()) {
                MessageBoxW(App::hwnd, L"Выберите группу в списке.", L"LEFTIK Diff", MB_OK | MB_ICONINFORMATION);
                break;
            }
            if (App::selectedGroups.size() > 1) {
                MessageBoxW(App::hwnd, L"Выберите ровно одну группу для просмотра diff.", L"LEFTIK Diff", MB_OK | MB_ICONINFORMATION);
                break;
            }
            std::wstring grp = *App::selectedGroups.begin();
            auto diffs = Journal::BuildDiffForGroup(grp);
            int choice = ShowDiffModalDialog(App::hwnd, grp, diffs);
            if (choice == 1 || choice == 2) {
                int r = 0, rf = 0, cr = 0, cf = 0, uns = 0;
                int res = Journal::RollbackGroupInternal(grp, choice == 1, r, rf, cr, cf, uns);
                for (auto& tw : App::tweaksList) tw.isEnabled = ReadTweakState(tw.id);
                Monitor::InvalidateGroupsCache();
                App::cachedGroups = Monitor::GetGroupsSnapshot();
                if (res == 0) App::selectedGroups.erase(grp);
                InvalidateRect(App::hwnd, nullptr, FALSE);
                wchar_t msg[512];
                swprintf(msg, 512,
                    L"Откат группы [%ls] завершён (код %d).\nВосстановлено реестра: %d, команд: %d\nОшибок реестра: %d, ошибок команд: %d",
                    grp.c_str(), res, r, cr, rf, cf);
                MessageBoxW(App::hwnd, msg, L"LEFTIK Diff Rollback", MB_OK | (res == 0 ? MB_ICONINFORMATION : MB_ICONWARNING));
            }
            break;
        }
        case 3307: {
            SYSTEMTIME st{}; GetLocalTime(&st);
            wchar_t fileBuf[MAX_PATH]{};
            swprintf(fileBuf, MAX_PATH, L"leftik_export_%04d%02d%02d_%02d%02d%02d.leftik",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            OPENFILENAMEW ofn{ sizeof(ofn) };
            ofn.hwndOwner = App::hwnd;
            ofn.lpstrFilter = L"LEFTIK Journal Export (*.leftik)\0*.leftik\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"leftik";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&ofn)) {
                if (Journal::ExportJournal(fileBuf)) {
                    MessageBoxW(App::hwnd, (L"Журнал успешно экспортирован с HMAC-подписью:\n" + std::wstring(fileBuf)).c_str(), L"LEFTIK Export", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(App::hwnd, L"Ошибка экспорта журнала.", L"LEFTIK Export", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }
        case 3308: {
            wchar_t fileBuf[MAX_PATH]{};
            OPENFILENAMEW ofn{ sizeof(ofn) };
            ofn.hwndOwner = App::hwnd;
            ofn.lpstrFilter = L"LEFTIK Journal Export (*.leftik)\0*.leftik\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = fileBuf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                int validE = 0, tamperedE = 0;
                std::vector<std::string> validLines;
                int rc = Journal::ImportJournalEx(fileBuf, validE, tamperedE, false, &validLines);
                if (rc == 0) {
                    Monitor::InvalidateGroupsCache();
                    App::cachedGroups = Monitor::GetGroupsSnapshot();
                    InvalidateRect(App::hwnd, nullptr, FALSE);
                    wchar_t msg[256];
                    swprintf(msg, 256, L"Импортировано %d записей.", validE);
                    MessageBoxW(App::hwnd, msg, L"LEFTIK Import", MB_OK | MB_ICONINFORMATION);
                } else if (rc == 2) {
                    MessageBoxW(App::hwnd, L"Журнал создан на другой машине. Импорт невозможен.", L"LEFTIK Import", MB_OK | MB_ICONERROR);
                } else if (rc == 3) {
                    MessageBoxW(App::hwnd, L"Файл открыт, но записей 0.", L"LEFTIK Import", MB_OK | MB_ICONINFORMATION);
                } else if (rc == 1 && validE > 0) {
                    wchar_t ask[320];
                    swprintf(ask, 320, L"%d записей повреждены, отброшены. Продолжить с %d валидными?", tamperedE, validE);
                    if (MessageBoxW(App::hwnd, ask, L"LEFTIK Import", MB_YESNO | MB_ICONWARNING) == IDYES) {
                        int v2 = 0, t2 = 0;
                        Journal::ImportJournalEx(fileBuf, v2, t2, true, nullptr);
                        Monitor::InvalidateGroupsCache();
                        App::cachedGroups = Monitor::GetGroupsSnapshot();
                        InvalidateRect(App::hwnd, nullptr, FALSE);
                        wchar_t doneMsg[256];
                        swprintf(doneMsg, 256, L"Импортировано %d записей.", v2);
                        MessageBoxW(App::hwnd, doneMsg, L"LEFTIK Import", MB_OK | MB_ICONINFORMATION);
                    }
                } else {
                    MessageBoxW(App::hwnd, L"Файл журнала повреждён или подпись HMAC не сошлась.", L"LEFTIK Import", MB_OK | MB_ICONERROR);
                }
            }
            break;
        }
        case 3309: {
            if (g_lastClickedGroupIdx < App::cachedGroups.size()) {
                const std::wstring& grp = App::cachedGroups[g_lastClickedGroupIdx];
                if (!App::selectedGroups.erase(grp)) {
                    App::selectedGroups.insert(grp);
                }
                InvalidateRect(App::hwnd, nullptr, FALSE);
            }
            break;
        }
        case 3310: {
            if (App::selectedGroups.empty()) {
                MessageBoxW(App::hwnd, L"Выберите хотя бы одну группу для отката.", L"LEFTIK", MB_OK | MB_ICONINFORMATION);
                break;
            }
            wchar_t confirmMsg[256];
            swprintf(confirmMsg, 256, L"Откатить выбранные группы (%d шт.)?", (int)App::selectedGroups.size());
            if (MessageBoxW(App::hwnd, confirmMsg, L"LEFTIK Selective Rollback", MB_YESNO | MB_ICONQUESTION) != IDYES) break;

            int totalRestored = 0, totalRegFailed = 0, totalCmdRestored = 0, totalCmdFailed = 0, totalUns = 0;
            std::vector<std::wstring> toRollback(App::selectedGroups.begin(), App::selectedGroups.end());
            for (const auto& grp : toRollback) {
                int r = 0, rf = 0, cr = 0, cf = 0, uns = 0;
                int res = Journal::RollbackGroup(grp, r, rf, cr, cf, uns);
                totalRestored += r; totalRegFailed += rf;
                totalCmdRestored += cr; totalCmdFailed += cf;
                totalUns += uns;
                if (res == 0) App::selectedGroups.erase(grp);
            }
            for (auto& tw : App::tweaksList) tw.isEnabled = ReadTweakState(tw.id);
            Monitor::InvalidateGroupsCache();
            App::cachedGroups = Monitor::GetGroupsSnapshot();
            InvalidateRect(App::hwnd, nullptr, FALSE);

            wchar_t rep[512];
            swprintf(rep, 512,
                L"Выборочный откат завершён.\n\nВосстановлено значений реестра: %d\nВыполнено команд: %d\nОшибок реестра: %d, ошибок команд: %d, неподдерживаемых: %d",
                totalRestored, totalCmdRestored, totalRegFailed, totalCmdFailed, totalUns);
            MessageBoxW(App::hwnd, rep, L"LEFTIK Selective Rollback",
                        MB_OK | ((totalRegFailed == 0 && totalCmdFailed == 0 && totalUns == 0) ? MB_ICONINFORMATION : MB_ICONWARNING));
            break;
        }
        case 3300: {
            if (!IsUserAnAdmin()) {
                MessageBoxW(App::hwnd, L"Запустите от администратора", L"Нет прав", MB_OK | MB_ICONWARNING);
                break;
            }
            int rbResult = Journal::Rollback(App::hwnd); 
            if (rbResult == 2) {
                break; // Пользователь нажал "Нет" в диалоге — ничего не трогаем
            }
            if (rbResult == 3) {
                // Журнал пуст — реального отката НЕ было. Если активен профиль, предупреждаем,
                // но его состояние и таймер НЕ сбрасываем вслепую.
                if (App::isMaxActive || App::isUltraActive || App::magicBoostActive || App::turboBoostActive) {
                    MessageBoxW(App::hwnd,
                        L"ВНИМАНИЕ: журнал пуст, но игровой профиль сейчас активен.\n"
                        L"Профиль НЕ был сброшен. Выключите его вручную на Главной.",
                        L"LEFTIK", MB_OK | MB_ICONWARNING);
                }
                break;
            }
            if (rbResult == 4) {
                // Хуки tweaks не установлены (InitTweaks ещё не выполнен) — журнал не тронут.
                MessageBoxW(App::hwnd,
                    L"Модуль твиков ещё не инициализирован.\n"
                    L"Откат отменён, журнал не изменён.",
                    L"LEFTIK", MB_OK | MB_ICONWARNING);
                break;
            }
            // Полный откат стирает журнал вместе с маркерами профилей — гасим их владельцев
            // таймера, чтобы флаги не расходились с реальным состоянием
            TimerMgr::Release(L"profile-pro");
            TimerMgr::Release(L"profile-ultra");
            TimerMgr::Release(L"smart3000");
            TimerMgr::Release(L"tweak1080");
            TimerMgr::Release(L"magic-boost");
            TimerMgr::Release(L"turbo-boost");
            App::isMaxActive = false;
            App::isUltraActive = false;
            App::magicBoostActive = false;
            App::turboBoostActive = false;
            RevertTweak(1020);  // Ultimate Performance
            InvalidateRect(App::hwnd, nullptr, FALSE);
            if (rbResult != 0) {
                MessageBoxW(App::hwnd,
                    L"Откат выполнен ЧАСТИЧНО.\n\n"
                    L"См. подробности в предыдущем окне. Журнал сохранён,\n"
                    L"ряд операций требует ручной проверки.",
                    L"LEFTIK", MB_OK | MB_ICONWARNING);
            }
            break;
        }
        
        default:
            if (id >= 3600 && id < 3700) {
                g_lastClickedGroupIdx = static_cast<size_t>(id - 3600);
                DispatchAction(3309);
                break;
            }
            if (id >= 4000 && id < 4500) {
                int gameIdx = id - 4000;
                std::vector<App::Game> supportedGames = App::steamGames;
                if (gameIdx < (int)supportedGames.size()) {
                    std::wstring name = supportedGames[gameIdx].name;
                    std::wstring commonDir = Util::Join(supportedGames[gameIdx].path, L"steamapps\\common");
                    if (name.find(L"Counter-Strike") != std::wstring::npos) {
                        ApplyCS2Config();
                        ApplyCS2LaunchOptions();
                        // Правильный путь: steamapps\common\<installdir из appmanifest>
                        std::wstring dir = supportedGames[gameIdx].installdir.empty() ? L"Counter-Strike Global Offensive" : supportedGames[gameIdx].installdir;
                        std::wstring exePath = Util::Join(commonDir, dir + L"\\game\\bin\\win64\\cs2.exe");
                        if (Util::FileExists(exePath)) CreateOptimizedShortcut(exePath, name);
                        else MessageBoxW(App::hwnd, (L"Ярлык не создан — cs2.exe не найден:\n" + exePath).c_str(), L"LEFTIK", MB_OK);
                    } else if (name.find(L"Dota 2") != std::wstring::npos) {
                        ApplyDota2Config();
                        ApplyDota2LaunchOptions();
                        std::wstring dir = supportedGames[gameIdx].installdir.empty() ? L"dota 2 beta" : supportedGames[gameIdx].installdir;
                        std::wstring exePath = Util::Join(commonDir, dir + L"\\game\\bin\\win64\\dota2.exe");
                        if (Util::FileExists(exePath)) CreateOptimizedShortcut(exePath, name);
                        else MessageBoxW(App::hwnd, (L"Ярлык не создан — dota2.exe не найден:\n" + exePath).c_str(), L"LEFTIK", MB_OK);
                    } else if (name.find(L"Arizona") != std::wstring::npos) {
                        ApplyArizonaConfig(); // GPU-приоритет для gta_sa.exe и samp.exe
                        ApplyArizonaLauncherHints();

                        // Ярлык создаем на ЛАУНЧЕР (без него игра не запустится), а не на gta_sa.exe
                        std::wstring launcherPath = Util::Join(supportedGames[gameIdx].path, L"Arizona Games Launcher.exe");
                        if (!Util::FileExists(launcherPath))
                            launcherPath = FindGameExe(supportedGames[gameIdx].path, L"Arizona Games Launcher.exe");

                        if (!launcherPath.empty()) {
                            CreateOptimizedShortcut(launcherPath, name);
                        } else {
                            // Лаунчер не найден — fallback: ярлык хотя бы на gta_sa.exe
                            std::wstring exePath = FindGameExe(supportedGames[gameIdx].path, L"gta_sa.exe");
                            if (!exePath.empty()) CreateOptimizedShortcut(exePath, name);
                            else MessageBoxW(App::hwnd, (L"Не найден ни лаунчер, ни gta_sa.exe.\nПапка: " + supportedGames[gameIdx].path).c_str(), L"LEFTIK", MB_OK);
                        }
                    } else {
                        T_GameDVR(); T_Priority(); T_HwSchMode();
                        MemoryOptimizer::ClearStandbyList();
                        // Display name != имя процесса. Ищем реальный exe: installdir[.exe] или самый крупный .exe в папке.
                        std::wstring gameDir = supportedGames[gameIdx].installdir.empty()
                            ? L"" : Util::Join(commonDir, supportedGames[gameIdx].installdir);
                        std::wstring exeForPref;
                        if (!supportedGames[gameIdx].installdir.empty()) {
                            std::wstring raw = supportedGames[gameIdx].installdir;
                            std::wstring noSpace = raw;
                            noSpace.erase(std::remove(noSpace.begin(), noSpace.end(), L' '), noSpace.end());
                            if (Util::FileExists(Util::Join(gameDir, raw + L".exe")))
                                exeForPref = raw + L".exe";
                            else if (Util::FileExists(Util::Join(gameDir, noSpace + L".exe")))
                                exeForPref = noSpace + L".exe";
                        }
                        if (exeForPref.empty() && !gameDir.empty()) {
                            uintmax_t bestSize = 0;
                            std::error_code e;
                            auto it2 = std::filesystem::recursive_directory_iterator(
                                gameDir, std::filesystem::directory_options::skip_permission_denied, e);
                            std::filesystem::recursive_directory_iterator endIt;
                            while (!e && it2 != endIt) {
                                std::error_code ee;
                                if (it2->is_regular_file(ee) && it2->path().extension() == L".exe") {
                                    auto sz = it2->file_size(ee);
                                    if (!ee && sz > bestSize && sz > (1024ull * 1024ull)) {
                                        bestSize = sz; exeForPref = it2->path().filename().wstring();
                                    }
                                }
                                it2.increment(e);
                            }
                        }
                        if (!exeForPref.empty()) SetGameHighPerformanceGPU(exeForPref);
                        MessageBoxW(App::hwnd, (L"Общая оптимизация для " + name + L" применена!\nGpuPreference: " + (exeForPref.empty() ? std::wstring(L"exe не найден") : exeForPref)).c_str(), L"LEFTIK", MB_OK);
                    }
                }
            }
            if (id >= 4500 && id < 4600) {
                int gameIdx = id - 4500;
                std::vector<App::Game> supportedGames = App::steamGames;
                if (gameIdx < (int)supportedGames.size()) {
                    ShowGameConfig(supportedGames[gameIdx].name);
                }
            }
            if (id >= 4600 && id < 4700) {
                int gameIdx = id - 4600;
                if (gameIdx < (int)App::steamGames.size()) {
                    std::wstring gpu = App::gpuName;
                    std::transform(gpu.begin(), gpu.end(), gpu.begin(), ::towlower);
                    bool isNv = gpu.find(L"nvidia") != std::wstring::npos;
                    bool isAmd = gpu.find(L"amd") != std::wstring::npos || gpu.find(L"radeon") != std::wstring::npos;

                    std::wstring msg;
                    if (isNv) {
                        msg = L"🎮 Настройки NVIDIA для CS2:\n\n"
                              L"В САМОЙ ИГРЕ (CS2):\n"
                              L"1. Настройки видео → NVIDIA Reflex: Вкл (или Вкл+Boost)\n\n"
                              L"В NVIDIA APP (Graphics → Program Settings → CS2):\n"
                              L"1. Low Latency Mode: ВЫКЛ (Reflex в игре работает лучше)\n"
                              L"2. Power Management Mode: Prefer Maximum Performance\n"
                              L"3. Vertical Sync: ВЫКЛ\n"
                              L"4. Texture Filtering - Quality: High Performance\n"
                              L"5. Max Frame Rate: ВЫКЛ (или 141 при мониторе 144 Гц)\n\n"
                              L"ВАЖНО: Reflex 2 / Frame Warp анонсирован в январе 2025, но на 2026 "
                              L"всё ещё НЕ ВЫПУЩЕН и не используется ни одной игрой. Не ищите его "
                              L"в настройках — используйте Reflex 1.";
                    } else if (isAmd) {
                        msg = L"🎮 Настройки AMD для CS2:\n\n"
                              L"В САМОЙ ИГРЕ (CS2):\n"
                              L"1. Настройки видео → AMD Anti-Lag 2 (в новых драйверах — "
                              L"FSR Latency Reduction 2.0): Вкл\n\n"
                              L"В AMD ADRENALIN (Gaming → Games → CS2):\n"
                              L"1. Radeon Chill: ВЫКЛ (ломает фреймтайм)\n"
                              L"2. Radeon Boost: ВЫКЛ (размывает картинку)\n"
                              L"3. Radeon Anti-Lag (драйверный): ВЫКЛ (юзай Anti-Lag 2 в игре)\n"
                              L"4. HYPR-RX: ВЫКЛ (сбрасывает настройки игры)\n"
                              L"5. RSR / AFMF: ВЫКЛ (добавляют задержку)\n\n"
                              L"ВАЖНО: AMD переименовала Anti-Lag 2 в FSR Latency Reduction 2.0 "
                              L"в новых драйверах. Поддерживается в CS2, Dota 2, Deadlock и др.";
                    } else {
                        msg = L"В САМОЙ ИГРЕ (CS2):\n1. Включи любые настройки Low Latency (если есть).\n2. Отключи V-Sync и Triple Buffering.\n3. Поставь режим полного экрана (Fullscreen).";
                    }
                    msg += L"\n\n⚠️ ВАЖНО:\nНастройки из драйвера (Low Latency Mode, Chill, Boost, HYPR-RX) конфликтуют с внутриигровыми технологиями. Оставь только одну (лучше внутриигровую Reflex / Anti-Lag 2).";
                    msg += L"\n\n💡 СОВЕТ ПО ПОТОКАМ (как в Hanami):\nНЕ ОГРАНИЧИВАЙТЕ CS2 одним потоком на ядро через сторонний софт. Движок Source 2 сам идеально распределяет рендер, сеть и звук. Принудительное ограничение (Affinity) вызовет фризы и просадку FPS.";
                    MessageBoxW(App::hwnd, msg.c_str(), L"LEFTIK — Гайд по GPU", MB_OK | MB_ICONINFORMATION);
                }
            }
            break;
    }
}

void MakeWindowRounded(HWND hwnd) {
    int preference = 2; // DWMWCP_ROUND (Windows 11)
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));
    if (App::hwInfo.winBuild < 22000) {
        // Регион задаётся в физических пикселях, а windowW/H — логические.
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            HRGN hRgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, Scale(20), Scale(20));
            SetWindowRgn(hwnd, hRgn, TRUE);
        }
    }
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

// Блок P: при уходе в трей освобождаем GDI/GDI+ ресурсы и рабочее множество.
// ВАЖНО: сначала удаляем DC, потом bitmap — пока bitmap выбран в DC,
// DeleteObject для него завершается ошибкой (утечка).
void ReleaseUiResourcesForTray() {
    for (auto& pair : App::gameImages) { if (pair.second) delete pair.second; }
    App::gameImages.clear();
    // DeleteDC снимает выбор bitmap с DC, после этого DeleteObject корректен.
    if (App::hBufferDC)  { DeleteDC(App::hBufferDC);      App::hBufferDC  = nullptr; }
    if (App::hBufferBmp) { DeleteObject(App::hBufferBmp); App::hBufferBmp = nullptr; }
    EmptyWorkingSet(GetCurrentProcess());
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        App::hwnd = h;
        g_dpi = GetDpiForWindow(h);
        if (g_dpi <= 0) g_dpi = 96;
        {
            // WM_SIZE мог прийти раньше WM_CREATE — приводим размер к логическим px.
            RECT rc{};
            GetClientRect(h, &rc);
            if (rc.right > 0 && rc.bottom > 0) {
                App::windowW = Unscale(rc.right);
                App::windowH = Unscale(rc.bottom);
            }
        }
        {
            HWND hwnd = h;
            SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
            COLORREF captionColor = RGB(20, 20, 25); // Темный цвет сайдбара
            COLORREF textColor = RGB(240, 240, 245); // Белый текст
            COLORREF borderColor = RGB(20, 20, 25); // Цвет рамки
            DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor)); // DWMWA_CAPTION_COLOR
            DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor)); // DWMWA_TEXT_COLOR
            DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor)); // DWMWA_BORDER_COLOR
        }
        App::fontFamily = new FontFamily(L"Segoe UI");
        App::fRegular  = new Font(App::fontFamily, 12.0f, FontStyleRegular, UnitPixel);
        App::fBold     = new Font(App::fontFamily, 14.0f, FontStyleBold, UnitPixel);
        App::fTitle    = new Font(App::fontFamily, 22.0f, FontStyleBold, UnitPixel);
        App::fSmall    = new Font(App::fontFamily, 11.0f, FontStyleRegular, UnitPixel);
        App::fH1       = new Font(App::fontFamily, 24.0f, FontStyleBold, UnitPixel);
        App::fH2       = new Font(App::fontFamily, 18.0f, FontStyleBold, UnitPixel);
        App::fMetric   = new Font(App::fontFamily, 28.0f, FontStyleBold, UnitPixel);
        App::fNav      = new Font(App::fontFamily, 14.0f, FontStyleRegular, UnitPixel);
        App::fBody     = new Font(App::fontFamily, 13.0f, FontStyleRegular, UnitPixel);
        App::fBodyBold = new Font(App::fontFamily, 13.0f, FontStyleBold, UnitPixel);
        App::iconFamily = new FontFamily(L"Segoe MDL2 Assets");
        App::fIcon    = new Font(App::iconFamily, 16.0f, FontStyleRegular, UnitPixel);
        App::fIconBig = new Font(App::iconFamily, 20.0f, FontStyleRegular, UnitPixel);

        InitTweaks();
        LoadTweakStates();
        LoadBackgroundMode();
        LoadStandbySettings();  // v58.0 Feature B
        App::dnsPromptedThisSession = false;  // v58.0 Feature A
        App::exitRequested = false;
        GetHardwareInfo();
        ScanGames();

        // Начальное состояние питания: неизвестное (255) считаем сетью,
        // чтобы не сработал ложный Auto-Guard при старте.
        {
            SYSTEM_POWER_STATUS sps{};
            if (GetSystemPowerStatus(&sps)) {
                App::lastPowerState = (sps.ACLineStatus != 0);
                App::hwInfo.onACPower = App::lastPowerState;
            }
        }

        // Иконка в трее
        {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = h; nid.uID = 1;
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_LEFTIK));
            if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            wcscpy_s(nid.szTip, L"LEFTIK Optimizer - Win+Shift+B: boost");
            Shell_NotifyIconW(NIM_ADD, &nid);
        }
        RegisterHotKey(h, HOTKEY_BOOST, MOD_WIN | MOD_SHIFT, 'B');
        Monitor::Start();
        SetTimer(h, 1, 1500, nullptr);
        PostMessageW(h, WM_APP + 10, 0, 0);
        return 0;
    case WM_NCCALCSIZE:
        if (w == TRUE) {
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
            if (IsZoomed(h)) {
                HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{ sizeof(mi) };
                if (GetMonitorInfoW(mon, &mi)) {
                    p->rgrc[0] = mi.rcWork; // Не залезаем под панель задач при развороте
                }
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &pt);
        // Хит-тест живёт в логических px, как и App::windowW/H.
        // После Unscale() координаты уже логические, поэтому и константы
        // должны быть логическими — повторный Scale() здесь дал бы двойное
        // масштабирование и увёл бы зоны кнопок/заголовка на HiDPI.
        pt.x = Unscale(pt.x);
        pt.y = Unscale(pt.y);
        const int border = 6;
        const int titleH = 32;
        const int buttonsW = 138;
        const bool zoomed = (IsZoomed(h) != FALSE);

        // 1. Ресайз за все края и углы (когда окно не развернуто)
        if (!zoomed) {
            if (pt.y < border && pt.x < border) return HTTOPLEFT;
            if (pt.y < border && pt.x >= App::windowW - border) return HTTOPRIGHT;
            if (pt.y >= App::windowH - border && pt.x < border) return HTBOTTOMLEFT;
            if (pt.y >= App::windowH - border && pt.x >= App::windowW - border) return HTBOTTOMRIGHT;
            if (pt.y < border) return HTTOP;
            if (pt.y >= App::windowH - border) return HTBOTTOM;
            if (pt.x < border) return HTLEFT;
            if (pt.x >= App::windowW - border) return HTRIGHT;
        }
        // 2. Кастомные кнопки: HTCLIENT! Иначе Windows нарисует кнопки Win95.
        if (pt.y >= 0 && pt.y < titleH && pt.x >= App::windowW - buttonsW) {
            return HTCLIENT;
        }
        // 3. Зона перетаскивания
        if (pt.y >= 0 && pt.y < titleH) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        if (g_dpi <= 0) g_dpi = 96;
        RECT* suggested = reinterpret_cast<RECT*>(l);
        if (suggested) {
            SetWindowPos(h, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_SIZE:
        if (w == SIZE_MINIMIZED) return 0;
        if (LOWORD(l) == 0 || HIWORD(l) == 0) return 0;
        // LOWORD/HIWORD — физические px. Layout работает в логических.
        App::windowW = Unscale(LOWORD(l));
        App::windowH = Unscale(HIWORD(l));
        if (App::hBufferDC)  { DeleteDC(App::hBufferDC);      App::hBufferDC  = nullptr; }
        if (App::hBufferBmp) { DeleteObject(App::hBufferBmp); App::hBufferBmp = nullptr; }
        g_bufW = g_bufH = 0;
        MakeWindowRounded(h);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (w == 1) {
            bool needRedraw = false;
            if (App::page == App::Dashboard || App::page == App::Sensors) {
                UpdateStats();
                needRedraw = true;
            }
            if (App::page == App::BenchmarkPage && Benchmark::IsRunning()) {
                needRedraw = true;
            }
            static int s_grpTick = 0;
            if (++s_grpTick >= 2) { // раз в 3 секунды (таймер 1500 мс) — без I/O, только копия из Monitor
                s_grpTick = 0;
                App::cachedGroups = Monitor::GetGroupsSnapshot();
                if (App::page == App::Restore) needRedraw = true;
            }
            static int s_hwTick = 0;
            if (++s_hwTick >= 30) {
                s_hwTick = 0;
                RefreshHardwareFlags();
                DetectDiskType(); // Повторная детекция SSD/HDD, если на старте диск был занят
                App::g_hardwareTier = DetectHardwareTier();
            }

            static bool s_prevGameActive = false;
            bool curGameActive = Monitor::gameActive.load(std::memory_order_relaxed);
            if (curGameActive != s_prevGameActive) {
                s_prevGameActive = curGameActive;
                needRedraw = true;
            }

            SYSTEM_POWER_STATUS sps{};
            if (GetSystemPowerStatus(&sps)) {
                bool currentAC = (sps.ACLineStatus == 1);
                App::hwInfo.onACPower = currentAC;
                if (currentAC != App::lastPowerState) {
                    App::lastPowerState = currentAC;
                    HandlePowerStateChange(currentAC);
                    needRedraw = true;
                }
            }
            if (needRedraw) InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        if (App::page == App::Tweaks) {
            App::tweaksScrollY += (delta > 0 ? 40 : -40);
            int maxScroll = TweaksMaxScroll();
            App::tweaksScrollY = std::clamp(App::tweaksScrollY, maxScroll, 0);
            InvalidateRect(h, nullptr, FALSE);
        } else if (App::page == App::Games) {
            App::gamesScrollY += (delta > 0 ? 40 : -40);
            int maxScroll = GamesMaxScroll();
            App::gamesScrollY = std::clamp(App::gamesScrollY, maxScroll, 0);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = Unscale(GET_X_LPARAM(l)), y = Unscale(GET_Y_LPARAM(l));

        // Debounce: игнорируем повторный клик в ту же точку (±3px) в течение 350ms.
        // Без этого double-click обрабатывается как два переключения (вкл-выкл),
        // и пользователь видит "тумблер не работает".
        static DWORD s_lastClickMs = 0;
        static POINT s_lastClickPt = { -1000, -1000 };
        DWORD nowMs = GetTickCount();
        int dx = x - s_lastClickPt.x, dy = y - s_lastClickPt.y;
        if (nowMs - s_lastClickMs < 350 && (dx*dx + dy*dy) <= 9) return 0;
        s_lastClickMs = nowMs; s_lastClickPt.x = x; s_lastClickPt.y = y;

        for (auto it = App::hits.rbegin(); it != App::hits.rend(); ++it) {
            if (x >= it->r.left && x < it->r.right && y >= it->r.top && y < it->r.bottom) { DispatchAction(it->id); break; }
        }
        return 0;
    }
    case WM_TRAYICON:
    {
        UINT msg = LOWORD(l);
        if (msg == WM_LBUTTONUP) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); }
        else if (msg == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Открыть");
            AppendMenuW(menu, MF_STRING, 2, L"Умная оптимизация (Win+Shift+B)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 3, L"Выход");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(h);
            int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
            DestroyMenu(menu);
            if (cmd == 1) { ShowWindow(h, SW_SHOW); SetForegroundWindow(h); }
            else if (cmd == 2) DispatchAction(3000);
            else if (cmd == 3) { App::exitRequested = true; PostMessageW(h, WM_CLOSE, 0, 0); }
        }
        return 0;
    }
    case WM_HOTKEY:
        if (w == HOTKEY_BOOST) DispatchAction(3000);
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(h, SW_HIDE);
            ReleaseUiResourcesForTray();
            return 0;
        }
        break;
    case (WM_APP + 10):
        EnsureFirstRunRestorePoint();
        return 0;
    case WM_LEFTIK_BENCH_STOPPED:
        // Сюда приходит и ручная, и авто-остановка ETW (wParam == 1).
        // Никаких модальных окон: бенчмарк не должен прерывать игру.
        // Итог авто-стопа UI покажет в статус-баре страницы «Бенчмарк».
        FpsMonitor::Stop();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case (WM_APP + 11): {
        ProcWaitStatus st = (ProcWaitStatus)w;
        DWORD ec = (DWORD)l;
        if (st == PWS_OK && ec == 0) {
            MessageBoxW(App::hwnd, 
                L"Точка восстановления успешно создана в фоновом режиме.\n"
                L"Проверить можно в Панели управления → Восстановление системы.",
                L"LEFTIK", MB_OK | MB_ICONINFORMATION);
        } else if (st == PWS_TIMEOUT) {
            MessageBoxW(App::hwnd, 
                L"Создание точки восстановления занимает больше времени, чем ожидалось.\n"
                L"Процесс продолжает работать в фоне. Проверьте позже вручную.",
                L"LEFTIK", MB_OK | MB_ICONWARNING);
        } else {
            MessageBoxW(App::hwnd,
                L"Команда создания точки восстановления отправлена, но результат не подтверждён.\n\n"
                L"ВНИМАНИЕ: Windows ограничивает частоту создания точек (throttle 24 часа).\n"
                L"Если точка создавалась недавно — новая молча НЕ создастся.",
                L"LEFTIK", MB_OK | MB_ICONWARNING);
        }
        return 0;
    }
    case WM_APP_JOBRESULT: {
        // Результат асинхронной операции: UI-поток показывает честный статус и освобождает память
        AsyncJobResult* r = (AsyncJobResult*)w;
        App::busyJob.store(false);
        if (r && r->jobId == 1) {
            if (r->status == (int)PWS_OK && r->verified)
                MessageBoxW(App::hwnd,
                    L"Компрессия памяти отключена (проверено: Get-MMAgent → False).\n\n"
                    L"Эффект зависит от системы: на 16+ ГБ сжатие чаще всего избыточно,\n"
                    L"на малых объёмах памяти оно может быть полезно. Откат — тумблером.",
                    L"LEFTIK Memory", MB_OK);
            else {
                std::wstring m = L"Отключение компрессии: результат НЕ подтверждён.\n" + (r->detail.empty() ? std::wstring(L"Неизвестная ошибка.") : r->detail);
                MessageBoxW(App::hwnd, m.c_str(), L"LEFTIK Memory", MB_OK | MB_ICONWARNING);
            }
        } else if (r && r->jobId == 2) {
            if (r->status == (int)PWS_OK && r->verified)
                MessageBoxW(App::hwnd,
                    L"TCP Autotuning = Restricted (проверено: netsh int tcp show global).\n\n"
                    L"Значение применено; влияние на пинг зависит от вашей сети.",
                    L"LEFTIK Network", MB_OK);
            else {
                std::wstring m = L"TCP Autotuning: результат НЕ подтверждён.\n" + (r->detail.empty() ? std::wstring(L"Неизвестная ошибка.") : r->detail);
                MessageBoxW(App::hwnd, m.c_str(), L"LEFTIK Network", MB_OK | MB_ICONWARNING);
            }
        }
        delete r;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE:
        // Фоновый режим: крестик прячет окно в трей, а не завершает программу.
        // Явный выход из трея (exitRequested) минует это поведение.
        if (App::backgroundMode && !App::exitRequested) {
            ShowWindow(h, SW_HIDE);
            ReleaseUiResourcesForTray();
            return 0;
        }

        // Feature A: спрашиваем ДО любых проверок — при реальном выходе всегда
        if (!PromptRestoreDnsBeforeExit()) {
            App::exitRequested = false;
            return 0;
        }

        // Буст активен — спрашиваем явно. Таймер освобождаем, флаг снимаем,
        // но твики не откатываем: пользователь может вернуть их тумблерами.
        if (App::magicBoostActive) {
            if (MessageBoxW(h,
                L"Буст MAX FPS активен.\n\n"
                L"При выходе таймер 0.5ms будет выключен, а применённые твики\n"
                L"останутся в системе (их можно вернуть тумблерами).\n\n"
                L"Выйти из LEFTIK?",
                L"LEFTIK MAX FPS", MB_YESNO | MB_ICONWARNING) != IDYES) {
                App::exitRequested = false;
                return 0;
            }
            TimerMgr::Release(L"magic-boost");
            App::magicBoostActive = false;
            App::exitRequested = true;
            DestroyWindow(h);
            return 0;
        }

        DestroyWindow(h);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        // Блок P: скрытое окно в трее не рисуем и не тратим CPU/память.
        if (!IsWindowVisible(h)) { EndPaint(h, &ps); return 0; }
        RECT rc{};
        GetClientRect(h, &rc);
        const int physW = rc.right, physH = rc.bottom;
        if (physW > 0 && physH > 0 &&
            (!App::hBufferBmp || !App::hBufferDC || g_bufW != physW || g_bufH != physH)) {
            if (App::hBufferDC)  { DeleteDC(App::hBufferDC);      App::hBufferDC  = nullptr; }
            if (App::hBufferBmp) { DeleteObject(App::hBufferBmp); App::hBufferBmp = nullptr; }
            App::hBufferDC = CreateCompatibleDC(dc);
            App::hBufferBmp = CreateCompatibleBitmap(dc, physW, physH);
            SelectObject(App::hBufferDC, App::hBufferBmp);
            g_bufW = physW;
            g_bufH = physH;
        }
        if (App::hBufferBmp && App::hBufferDC) {
            DrawWhole(App::hBufferDC);
            BitBlt(dc, 0, 0, physW, physH, App::hBufferDC, 0, 0, SRCCOPY);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DESTROY:
    {
        App::appClosing.store(true); // фоновые операции увидят и не будут писать в UI
        UnregisterHotKey(h, HOTKEY_BOOST);
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = h; nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        Monitor::Stop();

        // v58.0: аварийное освобождение ВСЕХ владельцев таймера 0.5ms.
        // Без этого выход при активном ULTRA/PRO/1080 оставлял таймер в системе.
        TimerMgr::Release(L"profile-pro");
        TimerMgr::Release(L"profile-ultra");
        TimerMgr::Release(L"smart3000");
        TimerMgr::Release(L"tweak1080");
        TimerMgr::Release(L"magic-boost");
        TimerMgr::Release(L"turbo-boost");
        TimerMgr::Release(L"gameguard");

        KillTimer(h, 1);
        // Порядок важен: сначала DC (освобождает bitmap), затем bitmap.
        if (App::hBufferDC)  { DeleteDC(App::hBufferDC);      App::hBufferDC  = nullptr; }
        if (App::hBufferBmp) { DeleteObject(App::hBufferBmp); App::hBufferBmp = nullptr; }
        for (auto& pair : App::gameImages) { if (pair.second) delete pair.second; }
        App::gameImages.clear();
        delete App::fIconBig;
        delete App::fBodyBold;
        delete App::fBody;
        delete App::fNav;
        delete App::fMetric;
        delete App::fH2;
        delete App::fH1;
        delete App::fIcon; delete App::iconFamily;
        delete App::fRegular; delete App::fBold; delete App::fTitle; delete App::fSmall; delete App::fontFamily;
        FpsMonitor::Stop();
        Benchmark::Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int show) {
    // --silent: автозапуск из трея. Разбираем ДО elevation, чтобы передать флаг дальше.
    bool silentStart = false;
    {
        std::wstring args = cmdLine ? cmdLine : L"";
        std::wstring lower = args;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](wchar_t c) { return (wchar_t)towlower(c); });
        size_t pos = lower.find(L"--silent");
        while (pos != std::wstring::npos) {
            bool leftOk = (pos == 0) || lower[pos - 1] == L' ' || lower[pos - 1] == L'\t';
            size_t after = pos + 8;
            bool rightOk = (after >= lower.size()) || lower[after] == L' ' || lower[after] == L'\t';
            if (leftOk && rightOk) { silentStart = true; break; }
            pos = lower.find(L"--silent", pos + 1);
        }
    }

    if (!IsUserAnAdmin()) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.lpParameters = cmdLine; // сохраняем --silent при перезапуске с правами
        // Окончательное решение о показе принимает уже elevated-экземпляр
        // (после чтения настройки фонового режима), поэтому не передаём SW_HIDE.
        sei.nShow = SW_SHOWNORMAL;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        if (!ShellExecuteExW(&sei)) {
            DWORD err = GetLastError();
            if (err == ERROR_CANCELLED) return 0;
            return 1;
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gdiplusStartupInput; ULONG_PTR gdiplusToken; GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    {
        PWSTR progData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &progData)) && progData) {
            std::wstring leftikDir = Util::Join(progData, L"LEFTIK");
            CoTaskMemFree(progData);
            CreateDirectoryW(leftikDir.c_str(), nullptr);
            App::journalPath  = Util::Join(leftikDir, L"leftik_journal.log");
            App::debugLogPath = Util::Join(leftikDir, L"leftik_debug.log");
        } else {
            CreateDirectoryW(L"C:\\ProgramData\\LEFTIK", nullptr);
            App::journalPath  = L"C:\\ProgramData\\LEFTIK\\leftik_journal.log";
            App::debugLogPath = L"C:\\ProgramData\\LEFTIK\\leftik_debug.log";
        }
        Journal::RebuildJournalIndex();
        Monitor::InvalidateGroupsCache();
        App::cachedGroups = Monitor::GetGroupsSnapshot();
    }

    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = App::CLASS_NAME;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_LEFTIK));
    if (!wc.hIcon) {
        // Fallback для portable-запуска без встроенного ресурса (старые сборки).
        std::wstring icoPath = Util::Join(Util::ExeDir(), L"leftik.ico");
        wc.hIcon = (HICON)LoadImageW(nullptr, icoPath.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    // Стартовый размер окна тоже в логических px: на 150% окно должно быть
    // физически больше, а не показывать тот же маленький layout.
    {
        const UINT sysDpi = GetDpiForSystem();
        g_dpi = (sysDpi > 0) ? (int)sysDpi : 96;
    }
    HWND h = CreateWindowExW(0, App::CLASS_NAME, App::TITLE, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, Scale(1400), Scale(850), nullptr, nullptr, hInst, nullptr);
    HWND hwnd = h;
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
    COLORREF captionColor = RGB(20, 20, 25); // Темный цвет сайдбара
    COLORREF textColor = RGB(240, 240, 245); // Белый текст
    COLORREF borderColor = RGB(20, 20, 25); // Цвет рамки
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor)); // DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor)); // DWMWA_TEXT_COLOR
    DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor)); // DWMWA_BORDER_COLOR
    MakeWindowRounded(h);
    // --silent + «Работать в фоне»: стартуем скрыто, только иконка в трее.
    if (silentStart && App::backgroundMode) {
        ShowWindow(h, SW_HIDE);
    } else {
        // Даже если родитель передал SW_HIDE, при выключенном фоновом режиме
        // окно должно быть видимым.
        ShowWindow(h, silentStart ? SW_SHOWNORMAL : show);
        UpdateWindow(h);
    }

    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return 0;
}
