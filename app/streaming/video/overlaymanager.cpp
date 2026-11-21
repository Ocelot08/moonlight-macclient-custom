#include "overlaymanager.h"
#include "path.h"

using namespace Overlay;

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf")),
    m_BatteryPercentage(0),
    m_BatteryCharging(false)
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xD0, 0xD0, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xCC, 0x00, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;

    // Fixed (not per-message dynamic) width for the hotkey toast and list, so the box
    // doesn't resize as ON/OFF status changes. Set to the longest line across every
    // hotkey toggle message, "W  Swap Command/Control keys: OFF" (measured 569px at
    // 28pt), plus 40px slack - re-measure and update this if the hotkey text ever changes.
    static const int HOTKEY_OVERLAY_FIXED_WIDTH = 609;

    m_Overlays[OverlayType::OverlayHotkeyToast].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayHotkeyToast].fontSize = 28;
    m_Overlays[OverlayType::OverlayHotkeyToast].fixedWidth = HOTKEY_OVERLAY_FIXED_WIDTH;

    m_Overlays[OverlayType::OverlayHotkeyList].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayHotkeyList].fontSize = 28;
    m_Overlays[OverlayType::OverlayHotkeyList].fixedWidth = HOTKEY_OVERLAY_FIXED_WIDTH;

    // Unlike the toast/list above, this one only ever shows one fixed piece of
    // text while visible, so there's no "jumping" concern to size against -
    // tightening to content (fixedWidth left at 0) is fine.
    m_Overlays[OverlayType::OverlayWindowModeHint].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayWindowModeHint].fontSize = 20;

    // Fixed width for just the percentage text (a battery icon is prepended to
    // this in prependBatteryIcon(), separately fixed-size there, so the box as a
    // whole still never resizes). Set to "100%" (measured 76px at 28pt), plus
    // 40px slack - re-measure and update this if the battery text ever changes.
    static const int BATTERY_OVERLAY_FIXED_WIDTH = 116;

    m_Overlays[OverlayType::OverlayBattery].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayBattery].fontSize = 28;
    m_Overlays[OverlayType::OverlayBattery].fixedWidth = BATTERY_OVERLAY_FIXED_WIDTH;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    SDL_utf8strlcpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setBatteryStatus(int percentage, bool charging)
{
    m_BatteryPercentage = percentage;
    m_BatteryCharging = charging;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    {
        QWriteLocker lock(&m_OverlayRendererLock);
        m_Renderer = renderer;
    }

    // notifyOverlayUpdated() is a no-op whenever m_Renderer is still null, with
    // nothing re-triggering it once a renderer does show up - so any overlay
    // enabled before the renderer was registered (for example one enabled up
    // front at session start, rather than toggled live by a hotkey after
    // streaming is already underway) would otherwise never actually render.
    // Replay every currently-enabled overlay now that we have one to catch up.
    if (renderer != nullptr) {
        for (int i = 0; i < OverlayType::OverlayMax; i++) {
            if (m_Overlays[i].enabled) {
                notifyOverlayUpdated((OverlayType)i);
            }
        }
    }
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    QReadLocker lock(&m_OverlayRendererLock);

    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // A fixed width means always render at exactly that width; otherwise size the
    // box to each message's own content (within the usual 1024px wrapping cap).
    bool hasFixedWidth = m_Overlays[type].fixedWidth > 0;
    int renderWrapWidth = hasFixedWidth ? m_Overlays[type].fixedWidth : 1024;

    SDL_Surface* renderedSurface = m_Overlays[type].enabled ?
        // The _Wrapped variant is required for line breaks to work
        RenderTextOutlinedWrapped(m_Overlays[type].font,
                                  m_Overlays[type].text,
                                  m_Overlays[type].color,
                                  {0, 0, 0, 255},
                                  4,
                                  renderWrapWidth,
                                  !hasFixedWidth)
        : nullptr;

    if (renderedSurface != nullptr && type == OverlayType::OverlayBattery) {
        renderedSurface = prependBatteryIcon(renderedSurface, m_BatteryPercentage, m_BatteryCharging);
    }

    // Exchange the old surface with the new one
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface,
        renderedSurface);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}

SDL_Surface* OverlayManager::RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth, bool tightenToContent) {
    if (text == nullptr || text[0] == '\0') {
        return nullptr;
    }

    int oldOutline = TTF_GetFontOutline(font);
    TTF_SetFontOutline(font, outlineWidth);

    // Verify that the string won't require wrapping (which could cause the outline and the text
    // to diverge due to different wrapping positions). While we're at it, remember the widest
    // line's extent (from this same measurement call, so it's consistent with whatever SDL_ttf
    // itself considers each line's width to be) in case the caller wants the box sized closer
    // to the actual content than its maximum wrapWidth.
    //
    // FIXME: We do this rather than just disabling wrapping entirely (wrapWidth = 0) because we
    // need further testing to ensure that all renderers can handle non-NPOT overlay textures.
    int tightWidth = 0;
    for (const QString& line : QString(text).split('\n')) {
        int extent, count;
        if (TTF_MeasureUTF8(font, line.toUtf8(), wrapWidth, &extent, &count) == 0) {
            if (count < line.size()) {
                // If it requires wrapping, render it without the outline
                TTF_SetFontOutline(font, oldOutline);
                return TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
            }

            tightWidth = qMax(tightWidth, extent);
        }
    }

    // When the caller wants a box tightened to this message's own content, leave generous
    // slack: outline rendering can extend a glyph's visual footprint beyond the advance-width
    // metrics used above, and a wrapLength set too close to the measured width risks wrapping
    // the last word early (as happened with a smaller margin here before). Never exceed the
    // caller's original maximum. Otherwise (fixed width), just render at exactly wrapWidth so
    // the box doesn't change size as the message content changes.
    int renderWidth = tightenToContent ? qMin(wrapWidth, tightWidth + 40) : wrapWidth;

    // Draw text twice, but outline is a bit bigger
    auto outlineSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, outlineColor, renderWidth);
    TTF_SetFontOutline(font, 0);
    auto textSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, renderWidth);
    TTF_SetFontOutline(font, oldOutline);

    if (outlineSurface == nullptr || textSurface == nullptr) {
        SDL_FreeSurface(outlineSurface);
        SDL_FreeSurface(textSurface);
        return nullptr;
    }

    // Merge the texts
    SDL_Rect dst = { outlineWidth, outlineWidth, textSurface->w, textSurface->h };
    SDL_BlitSurface(textSurface, nullptr, outlineSurface, &dst);

    SDL_FreeSurface(textSurface);

    // SDL_ttf's wrapped render tight-fits to the actual content regardless of wrapWidth
    // when nothing needs to wrap (verified: passing 1024 vs 4096 for the same short text
    // produces an identical, narrower surface). So for a fixed-width box, passing wrapWidth
    // through alone only sets an upper bound before wrapping kicks in - it does not by
    // itself make a shorter message's surface any wider. Pad it out here instead, left-
    // aligned, transparent on the right, so every message in a fixed-width overlay renders
    // at exactly the same box width.
    if (!tightenToContent && outlineSurface->w < wrapWidth) {
        SDL_Surface* padded = SDL_CreateRGBSurfaceWithFormat(0, wrapWidth, outlineSurface->h, 32, outlineSurface->format->format);
        if (padded != nullptr) {
            SDL_FillRect(padded, nullptr, SDL_MapRGBA(padded->format, 0, 0, 0, 0));

            // Straight copy (not blend) so the source's own alpha is preserved exactly
            SDL_SetSurfaceBlendMode(outlineSurface, SDL_BLENDMODE_NONE);
            SDL_Rect padDst = { 0, 0, outlineSurface->w, outlineSurface->h };
            SDL_BlitSurface(outlineSurface, nullptr, padded, &padDst);

            SDL_FreeSurface(outlineSurface);
            outlineSurface = padded;
        }
    }

    return outlineSurface;
}

// An outlined battery body with a solid nub, filled from the left by one of four
// fixed steps (empty/low/half/full) based on percentage thresholds - not a smooth
// gauge, just a discrete indicator - plus, when charging, a blocky lightning bolt
// drawn with its own thin black halo so it stays visible whether it lands over the
// filled (white) or unfilled (transparent) part of the body.
SDL_Surface* OverlayManager::prependBatteryIcon(SDL_Surface* textSurface, int percentage, bool charging)
{
    static const int BODY_W = 32;
    static const int BODY_H = 20;
    static const int BORDER = 3;
    static const int NUB_W = 4;
    static const int NUB_H = 8;
    static const int ICON_W = BODY_W + NUB_W;
    static const int ICON_H = BODY_H;
    static const int GAP = 10;

    int width = ICON_W + GAP + textSurface->w;
    int height = qMax(ICON_H, textSurface->h);

    SDL_Surface* composite = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, textSurface->format->format);
    if (composite == nullptr) {
        return textSurface;
    }

    SDL_FillRect(composite, nullptr, SDL_MapRGBA(composite->format, 0, 0, 0, 0));

    Uint32 white = SDL_MapRGBA(composite->format, 0xFF, 0xFF, 0xFF, 0xFF);
    Uint32 black = SDL_MapRGBA(composite->format, 0, 0, 0, 0xFF);
    Uint32 transparent = SDL_MapRGBA(composite->format, 0, 0, 0, 0);
    int iconY = (height - ICON_H) / 2;

    // Battery body, filled solid white to start
    SDL_Rect body = { 0, iconY, BODY_W, BODY_H };
    SDL_FillRect(composite, &body, white);

    // Four discrete fill steps rather than a continuous gauge: whatever portion
    // (from the right) isn't "filled" at this step gets punched transparent,
    // leaving a left-aligned solid bar of the stepped width (or none at all for
    // "empty", or the whole interior for "full", which just leaves the body as
    // solid as it already is above).
    SDL_Rect interior = { BORDER, iconY + BORDER, BODY_W - 2 * BORDER, BODY_H - 2 * BORDER };
    double fillFraction;
    if (percentage >= 60) {
        fillFraction = 1.0;
    }
    else if (percentage >= 30) {
        fillFraction = 0.5;
    }
    else if (percentage >= 10) {
        fillFraction = 0.25;
    }
    else {
        fillFraction = 0.0;
    }

    int filledW = (int)(interior.w * fillFraction);
    if (filledW < interior.w) {
        SDL_Rect unfilled = { interior.x + filledW, interior.y, interior.w - filledW, interior.h };
        SDL_FillRect(composite, &unfilled, transparent);
    }

    // Nub, solid (always shown, independent of fill level)
    SDL_Rect nub = { BODY_W, iconY + (BODY_H - NUB_H) / 2, NUB_W, NUB_H };
    SDL_FillRect(composite, &nub, white);

    if (charging) {
        // A blocky lightning bolt, sized to fit within the body's interior
        int ix = interior.x, iy = interior.y, iw = interior.w, ih = interior.h;
        SDL_Rect bolts[3] = {
            { ix + iw * 55 / 100, iy, iw * 35 / 100 + 1, ih * 55 / 100 + 1 },
            { ix + iw * 15 / 100, iy + ih * 45 / 100, iw * 35 / 100 + 1, ih * 55 / 100 + 1 },
            { ix + iw * 35 / 100, iy + ih * 35 / 100, iw * 30 / 100 + 1, ih * 30 / 100 + 1 },
        };

        for (const SDL_Rect& bolt : bolts) {
            SDL_Rect halo = { bolt.x - 1, bolt.y - 1, bolt.w + 2, bolt.h + 2 };
            SDL_FillRect(composite, &halo, black);
        }
        for (const SDL_Rect& bolt : bolts) {
            SDL_FillRect(composite, &bolt, white);
        }
    }

    // Blit the text alongside the icon, vertically centered
    SDL_SetSurfaceBlendMode(textSurface, SDL_BLENDMODE_NONE);
    SDL_Rect textDst = { ICON_W + GAP, (height - textSurface->h) / 2, textSurface->w, textSurface->h };
    SDL_BlitSurface(textSurface, nullptr, composite, &textDst);
    SDL_FreeSurface(textSurface);

    return composite;
}


