#include "CreateFontStyleImagesJob.hpp"

// rasterization of ExPoly
#include "libslic3r/SLA/AGGRaster.hpp"

// for get DPI
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include "slic3r/GUI/3DScene.hpp" // ::glsafe

using namespace Slic3r;
using namespace Slic3r::GUI;


CreateFontStyleImagesJob::CreateFontStyleImagesJob(
    FontManager::StyleImagesData &&input)
    : m_input(std::move(input))
{
    assert(m_input.result != nullptr);
    assert(!m_input.styles.empty());
    assert(m_input.max_width > 1);
}

void CreateFontStyleImagesJob::process(Ctl &ctl)
{    
    // create shapes and calc size (bounding boxes)
    std::vector<ExPolygons> name_shapes(m_input.styles.size());
    std::vector<double> scales(m_input.styles.size());
    images = std::vector<FontManager::StyleImage>(m_input.styles.size());

    for (auto &item : m_input.styles) {
        size_t index = &item - &m_input.styles.front();
        ExPolygons &shapes = name_shapes[index];
        shapes = Emboss::text2shapes(item.font, item.text.c_str(), item.prop);

        // create image description
        FontManager::StyleImage &image = images[index];
        BoundingBox &bounding_box = image.bounding_box;
        for (ExPolygon &shape : shapes)
            bounding_box.merge(BoundingBox(shape.contour.points));
        for (ExPolygon &shape : shapes) shape.translate(-bounding_box.min);
        
        // calculate conversion from FontPoint to screen pixels by size of font
        auto   mf  = wxGetApp().mainframe;
        // dot per inch for monitor
        int    dpi = get_dpi_for_window(mf);
        double ppm = dpi / 25.4; // pixel per milimeter
        double unit_per_em = item.font.font_file->unit_per_em;
        double scale = item.prop.size_in_mm / unit_per_em * Emboss::SHAPE_SCALE * ppm;
        scales[index] = scale;

        //double scale = font_prop.size_in_mm * SCALING_FACTOR;
        BoundingBoxf bb2(bounding_box.min.cast<double>(),
                         bounding_box.max.cast<double>());
        bb2.scale(scale);
        image.tex_size.x = std::ceil(bb2.max.x() - bb2.min.x());
        image.tex_size.y = std::ceil(bb2.max.y() - bb2.min.y());

        // crop image width
        if (image.tex_size.x > m_input.max_width) 
            image.tex_size.x = m_input.max_width;
    }

    // arrange bounding boxes
    int offset_y = 0;
    width        = 0;
    for (FontManager::StyleImage &image : images) {
        image.offset.y() = offset_y;
        offset_y += image.tex_size.y+1;
        if (width < image.tex_size.x) 
            width = image.tex_size.x;
    }
    height = offset_y;
    for (FontManager::StyleImage &image : images) {
        const Point &o = image.offset;
        const ImVec2 &s = image.tex_size;
        image.uv0 = ImVec2(o.x() / (double) width, 
                           o.y() / (double) height);
        image.uv1 = ImVec2((o.x() + s.x) / (double) width,
                           (o.y() + s.y) / (double) height);
    }

    // Set up result
    pixels = std::vector<unsigned char>(width * height, {0});

    // upload sub textures
    for (FontManager::StyleImage &image : images) {
        sla::Resolution resolution(image.tex_size.x, image.tex_size.y);
        size_t index = &image - &images.front();
        double pixel_dim = SCALING_FACTOR / scales[index];
        sla::PixelDim dim(pixel_dim, pixel_dim);
        double gamma = 1.;
        std::unique_ptr<sla::RasterBase> r =
            sla::create_raster_grayscale_aa(resolution, dim, gamma);
        for (const ExPolygon &shape : name_shapes[index]) r->draw(shape);
        
        // copy rastered data to pixels
        sla::RasterEncoder encoder = [&offset = image.offset, &pix = pixels, w=width,h=height]
        (const void *ptr, size_t width, size_t height, size_t num_components) {
            assert((offset.x() + width) <= w);
            assert((offset.y() + height) <= h);
            const unsigned char *ptr2 = (const unsigned char *) ptr;
            for (size_t x = 0; x < width; ++x)
                for (size_t y = 0; y < height; ++y) { 
                    size_t index = (offset.y() + y)*w + offset.x() + x;
                    assert(index < w * h);
                    pix[index]   = ptr2[y * width + x];
                }
            return sla::EncodedRaster();
        };
        r->encode(encoder);
    }
}

void CreateFontStyleImagesJob::finalize(bool canceled, std::exception_ptr &)
{
    // upload texture on GPU
    GLuint tex_id;
    GLenum target = GL_TEXTURE_2D, format = GL_ALPHA, type = GL_UNSIGNED_BYTE;
    GLint  level = 0, border = 0;
    glsafe(::glGenTextures(1, &tex_id));
    glsafe(::glBindTexture(target, tex_id));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GLint w = width, h=height;
    glsafe(::glTexImage2D(target, level, GL_ALPHA, w, h, border, format, type,
                          (const void *) pixels.data()));

    // set up texture id
    void *texture_id = (void *) (intptr_t) tex_id;        
    for (FontManager::StyleImage &image : images)
        image.texture_id = texture_id;
        
    // move to result
    m_input.result->styles = std::move(m_input.styles);
    m_input.result->images = std::move(images);

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));
}