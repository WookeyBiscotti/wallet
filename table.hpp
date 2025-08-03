#pragma once

#include <string>
#include <vector>

#include "renderer.hpp"

struct Vec2u {
    std::size_t x;
    std::size_t y;
};

enum class Align {
    LEFT,
    CENTER,
    RIGHT,
};

enum class Merge {
    NO,
    MASTER,
    SLAVE,
};

struct Cell {
    Align align = Align::CENTER;

    Merge merge = Merge::NO;
    Vec2u mergeSize = {1, 1};

    std::string text;
    Vec2u textSize;

    Vec2u pos;
    Vec2u size;
};

struct Table {
    std::vector<std::vector<Cell>> data;

    void pushRow() {
        setSize({data.size(), data.front().size() + 1});
    }

    void setSize(Vec2u c) {
        data.resize(c.x);
        for (auto& v : data) {
            v.resize(c.y);
        }
    }

    void setColumnAlign(std::size_t x, Align a) {
        for (std::size_t y = 0; y != data.front().size(); ++y) {
            data[x][y].align = a;
        }
    }

    void setContent(Vec2u c, std::string text) {
        data[c.x][c.y].text = std::move(text);
    }
    template<class V>
    void setContentLastRow(std::size_t x, V text) {
        data[x][data.front().size() - 1].text = std::to_string(text);
    }

    Cell& getCell(Vec2u c) {
        return data[c.x][c.y];
    }

    void render(const std::string& ouputFile) {
        std::size_t lineHeight = 0;
        std::vector<std::size_t> columnsWidth(data.size(), 0);

        for (std::size_t x = 0; x != data.size(); ++x) {
            for (std::size_t y = 0; y != data.front().size(); ++y) {
                auto& c = getCell({x, y});
                if (c.merge == Merge::SLAVE) {
                    continue;
                }
                std::tie(c.size.x, c.size.y) = calcTextSize(c.text);
                c.textSize = c.size;
                lineHeight = std::max(lineHeight, c.size.y);
                columnsWidth[x] = std::max(columnsWidth[x], c.size.x);
            }
        }

        std::size_t tableWidth = 0;
        std::size_t tableHeight = 0;
        for (std::size_t x = 0; x != data.size(); ++x) {
            tableHeight = 0;
            for (std::size_t y = 0; y != data.front().size(); ++y) {
                auto& c = getCell({x, y});
                c.pos = {DEFAULT_PADDING / 2 + tableWidth, tableHeight};
                c.size = {columnsWidth[x], lineHeight};
                tableHeight += lineHeight;
            }
            tableWidth += columnsWidth[x] + DEFAULT_PADDING;
        }

        for (std::size_t x = 0; x != data.size(); ++x) {
            for (std::size_t y = 0; y != data.front().size(); ++y) {
                calcCellsSize({x, y});
            }
        }

        int imageWidth = tableWidth + 2 * DEFAULT_PADDING;
        int imageHeight = tableHeight + 2 * DEFAULT_PADDING;

        auto surface = Cairo::ImageSurface::create(Cairo::Format::FORMAT_RGB24, imageWidth, imageHeight);
        auto cr = Cairo::Context::create(surface);

        cr->set_source_rgb(55 / 255.0f, 55 / 255.0f, 77 / 255.0f);
        cr->paint();

        Pango::FontDescription fontDesc(DEFAULT_FONT);
        fontDesc.set_size(DEFAULT_FONT_SIZE);

        auto layout = Pango::Layout::create(cr);
        layout->set_wrap(Pango::WRAP_CHAR);
        layout->set_font_description(fontDesc);
        Pango::AttrList list;
        Pango::Attribute attr = Pango::Attribute::create_attr_foreground(65535 * (222 / 255.0f), 65535 * (222 / 255.0f),
            65535 * (222 / 255.0f));
        list.insert(attr);
        layout->set_attributes(list);

        for (std::size_t x = 0; x != data.size(); ++x) {
            for (std::size_t y = 0; y != data.front().size(); ++y) {
                const auto& c = getCell({x, y});
                if (c.align == Align::RIGHT) {
                    cr->move_to(DEFAULT_PADDING + c.pos.x + (c.size.x - c.textSize.x), DEFAULT_PADDING + c.pos.y);
                } else {
                    cr->move_to(DEFAULT_PADDING + c.pos.x, DEFAULT_PADDING + c.pos.y);
                }

                layout->set_text(c.text);
                layout->show_in_cairo_context(cr);
            }
        }

        surface->write_to_png(ouputFile);
    }

private:
    void calcCellsSize(Vec2u cIdx) {
        auto& c = getCell(cIdx);
        if (c.merge == Merge::MASTER) {
            for (std::size_t x = cIdx.x + 1; x < cIdx.x + c.mergeSize.x; ++x) {
                for (std::size_t y = cIdx.y + 1; y < cIdx.y + c.mergeSize.y; ++y) {
                    auto cellSize = getCell({x, y});
                    c.size.x += cellSize.size.x;
                    c.size.y += cellSize.size.y;
                }
            }
        }
    }
};

template<>
inline void Table::setContentLastRow(std::size_t x, std::string text) {
    data[x][data.front().size() - 1].text = std::move(text);
}
template<>
inline void Table::setContentLastRow(std::size_t x, const char* text) {
    data[x][data.front().size() - 1].text = text;
}
template<>
inline void Table::setContentLastRow(std::size_t x, std::string_view text) {
    data[x][data.front().size() - 1].text = text;
}
