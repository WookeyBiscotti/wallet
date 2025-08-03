#pragma once

#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <pangomm.h>
#include <pangomm/fontface.h>
#include <pangomm/fontmap.h>

#include <string>

constexpr auto DEFAULT_FONT = "Noto Sans Mono";
constexpr auto DEFAULT_FONT_SIZE = 24 * PANGO_SCALE;
constexpr auto DEFAULT_PADDING = 20;

inline std::pair<std::size_t, std::size_t> calcTextSize(const std::string& text) {
    auto tempSurface = Cairo::ImageSurface::create(Cairo::Format::FORMAT_RGB24, 1, 1);
    auto tempCr = Cairo::Context::create(tempSurface);
    auto layout = Pango::Layout::create(tempCr);

    Pango::FontDescription fontDesc(DEFAULT_FONT);
    fontDesc.set_size(DEFAULT_FONT_SIZE);

    layout->set_wrap(Pango::WRAP_CHAR);
    layout->set_font_description(fontDesc);
    layout->set_text(text);

    int textWidth, textHeight;
    layout->get_pixel_size(textWidth, textHeight);

    return {textWidth, textHeight};
}

inline void drawImage(const std::string& text, const std::string& ouputFile) {
    auto tempSurface = Cairo::ImageSurface::create(Cairo::Format::FORMAT_RGB24, 1, 1);
    auto tempCr = Cairo::Context::create(tempSurface);
    auto layout = Pango::Layout::create(tempCr);

    Pango::FontDescription fontDesc("Noto Sans Mono");
    fontDesc.set_size(24 * PANGO_SCALE);

    layout->set_wrap(Pango::WRAP_CHAR);
    layout->set_font_description(fontDesc);
    layout->set_text(text);

    int textWidth, textHeight;
    layout->get_pixel_size(textWidth, textHeight);

    int imageWidth = textWidth + 2 * DEFAULT_PADDING;
    int imageHeight = textHeight + 2 * DEFAULT_PADDING;

    auto surface = Cairo::ImageSurface::create(Cairo::Format::FORMAT_RGB24, imageWidth, imageHeight);
    auto cr = Cairo::Context::create(surface);

    cr->set_source_rgb(55 / 255.0f, 55 / 255.0f, 77 / 255.0f);
    cr->paint();

    cr->move_to(DEFAULT_PADDING, DEFAULT_PADDING);

    layout = Pango::Layout::create(cr);
    layout->set_text(text);
    Pango::AttrList list;
    Pango::Attribute attr = Pango::Attribute::create_attr_foreground(65535 * (222 / 255.0f), 65535 * (222 / 255.0f),
        65535 * (222 / 255.0f));
    list.insert(attr);
    layout->set_attributes(list);
    layout->set_font_description(fontDesc);
    layout->show_in_cairo_context(cr);

    surface->write_to_png(ouputFile);
}
