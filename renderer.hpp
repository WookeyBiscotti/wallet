#pragma once

#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <pangomm.h>
#include <pangomm/fontface.h>
#include <pangomm/fontmap.h>
// #include <fontconfig/fontconfig.h>

#include <string>

inline void drawImage(const std::string& text, std::size_t w, std::size_t h) {
    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_RGB24, w, h);
    auto cr = Cairo::Context::create(surface);

    auto layout = Pango::Layout::create(cr);
    layout->update_from_cairo_context(cr);


    // FcConfigAppFontAddFile(FcConfigGetCurrent(), (const FcChar8 *)"font.ttf");
    Pango::FontDescription font_desc("Ubuntumono");
    font_desc.set_size(24 * PANGO_SCALE);

    layout->set_wrap(Pango::WRAP_CHAR); // Перенос по символам
    layout->set_font_description(font_desc);
    layout->set_text(text);

    auto ws = layout->get_width();
    auto hs = layout->get_height();

    int text_width, text_height;
    layout->get_pixel_size(text_width, text_height);

    double surface_width = surface->get_width();
    double surface_height = surface->get_height();

    double scale_x = surface_width / text_width;
    double scale_y = surface_height / text_height;
    double scale = std::min(scale_x, scale_y) * 0.9; // 90% от максимального размера

    cr->save();                                           // Сохраняем текущее состояние контекста
    cr->translate(surface_width / 2, surface_height / 2); // Центрируем
    cr->scale(scale, scale);                              // Масштабируем
    cr->translate(-text_width / 2, -text_height / 2);     // Корректируем позицию

    cr->set_source_rgb(25, 25, 25); // Черный цвет
    layout->show_in_cairo_context(cr);

    cr->restore(); // Восстанавливаем состояние

    // Save the surface to a file (optional)
    surface->write_to_png("output.png");
}
