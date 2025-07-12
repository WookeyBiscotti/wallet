#include <cairo.h>
#include <pango/pangocairo.h>

int main() {
    // Создаём поверхность Cairo (например, изображение PNG)
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 200);
    cairo_t *cr = cairo_create(surface);

    // Устанавливаем белый фон
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // Создаём Pango layout
    PangoLayout *layout;
    PangoFontDescription *desc;

    layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, "Привет, мир! Это текст с Pango и Cairo. 😃", -1);

    // Настраиваем шрифт
    desc = pango_font_description_from_string("Sans 24");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    // Устанавливаем цвет текста (чёрный)
    cairo_set_source_rgb(cr, 0, 0, 0);

    // Рисуем текст в позиции (20, 50)
    cairo_move_to(cr, 20, 50);
    pango_cairo_show_layout(cr, layout);

    // Освобождаем ресурсы
    g_object_unref(layout);
    cairo_destroy(cr);

    // Сохраняем в файл
    cairo_surface_write_to_png(surface, "text_output.png");
    cairo_surface_destroy(surface);

    return 0;
}
