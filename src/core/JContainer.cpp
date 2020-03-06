#include "include/core/JContainer.h"
#include "include/gpu/GrContext.h"
#include <iostream>
#include "include/gpu/gl/GrGLInterface.h"
#include <string>

JContainer::JContainer(int layerId, sk_sp<GrContext> context) {
    _ref = std::shared_ptr<JContainer>(this);
    _layerId = layerId;
    _context = context;
    font.setSize(24);
};

sharedJContainer JContainer::ref() {
    return _ref;
}

void JContainer::removeFromParent() {
    _parent->removeChild(_ref);
};

void JContainer::setParent(sharedJContainer parent) {
    _parent = parent;
};

void JContainer::addChild(sharedJContainer child) {
    if(child && (child->_width < _width || child->_height < _height)) {//probably not correct to set the children size to the parent's size :)
        child->resize(child->_width < _width ? _width : child->_width, child->_height < _height ? _height : child->_height);
    }
    _children.push_back(child);
    child->setParent(_ref);
};

void JContainer::removeAllChildren() {
    _children.clear();
};

int JContainer::getLayerId() {
    return _layerId;
};


void JContainer::removeChild(sharedJContainer child) {
    _children.erase(std::remove(_children.begin(), _children.end(), child), _children.end());
};

void JContainer::setX(SkScalar x) {
    _x = x;
}

void JContainer::setY(SkScalar y) {
    _y = y;
}

void JContainer::resize(SkScalar width, SkScalar height) {
    _width = width;
    _height = height;
    //resize means recreate SkSurface. Apparently not "too" expensive https://groups.google.com/forum/#!topic/skia-discuss/3c10MvyaSug
    SkImageInfo info = SkImageInfo::MakeN32(width, height, SkAlphaType::kPremul_SkAlphaType);
    auto oldsurface = _surface;
    _surface = SkSurface::MakeRenderTarget(_context.get(), SkBudgeted::kYes, info);
    _canvas = _surface->getCanvas();

    if(oldsurface) {
        oldsurface->draw(_canvas, 0, 0, &_paint);
        _canvas->flush();
    }
    
    for(int i = 0; i < _children.size(); i++) { //probably not correct to set the children size to the parent's size :)
        auto child = _children[i];
        if(child && (child->_width < _width || child->_height < _height)) {
            child->resize(child->_width < _width ? _width : child->_width, child->_height < _height ? _height : child->_height);
        }
    }

    _paint.setColor(SK_ColorGREEN);
    _canvas->drawRect(SkRect::MakeXYWH(0, 0, 3, 15), _paint);
    _canvas->drawRect(SkRect::MakeXYWH(0, 0, 15, 3), _paint);
    auto text = std::to_string(_layerId);
    _canvas->drawSimpleText(text.c_str(), text.length(), SkTextEncoding::kUTF8, 5, 25, font, _paint);
    _canvas->flush();


    // resize parent to be at least just as big. Except if it's layer 0
    // if(_parent && (_parent->_width < _width || _parent->_height < _height)
    //  && _layerId != 0) {
    //     std::cout << _parent->_width << " " << _width << ", " << _parent->_height << ", " << _height << std::endl;
    //     std::cout << "parent " << _parent->_layerId << " of child " << _layerId << " is too small. Resizing" << std::endl;
    //     _parent->resize(_parent->_width < _width ? _width : _parent->_width, _parent->_height < _height ? _height : _parent->_height);
    // }
}

SkScalar JContainer::x() {
    return _x;
}

SkScalar JContainer::y() {
    return _y;
}

sk_sp<SkSurface> JContainer::getSurface() {
    return _surface;
}

// void JContainer::setSurface(sk_sp<SkSurface> surface) {
//     _surface = surface;
//     _canvas = _surface->getCanvas();
//     _context = sk_sp<GrContext>(_canvas->getGrContext());
//     _width = surface->width();
//     _height = surface->height();
// }

// void JContainer::setContext(sk_sp<GrContext> context) {
//     _context = context;
//     SkImageInfo info = SkImageInfo::MakeN32(_width, _height, SkAlphaType::kPremul_SkAlphaType);
//     auto oldsurface = _surface;
//     _surface = SkSurface::MakeRenderTarget(_context.get(), SkBudgeted::kYes, info);
//     _canvas = _surface->getCanvas();

//     if(oldsurface) {
//         oldsurface->draw(_canvas, 0, 0, &_paint);
//         _canvas->flush();
//     }
// };

void JContainer::draw(bool clear) {
    if(!_surface && _children.size() == 0) return; // no children and no size, do nothing.
    if(clear) _canvas->clear(SK_ColorWHITE);
    for (size_t i = 0; i < _children.size(); i++)
    {
        auto child = _children.at(i);
        child->draw();
        auto childSurface = child->getSurface();
        if(childSurface) { // child has width&height?
            if(!_surface) { // if parent has no size, resize to child size
                resize(child->_width, child->_height);
            }
            // std::cout << _layerId << ", " << child->_layerId << std::endl;
            // std::cout << child->x() + child->_width << ", " << _surface->width() << std::endl;
            // std::cout << child->y() + child->_height << ", " << _surface->height() << std::endl;
            // _surface->flush();
            // childSurface->flush();
            // if(child->x() + child->_width <= _surface->width() && child->y() + child->_height <= _surface->height())
                childSurface->draw(_surface->getCanvas(), child->x(), child->y(), &_paint);
                // childSurface->flush();
            childSurface->flush();
        }
    }
    _surface->flush();
};

// void JContainer::flushloop() {
//         std::cout << "flushloop " << std::endl;

//     while(true) {
//         if(_needsFlush) {
//             _surface->flush();
//             _needsFlush = false;
//         }
//         std::this_thread::sleep_for (std::chrono::milliseconds(1000/20));
//     }
// };

// void JContainer::setNeedsFlush() {
//     std::cout << "setNeedsFlush " << std::endl;
//     #ifdef __EMSCRIPTEN_PTHREADS__
//     std::cout << "enabled pthreads" << std::endl;
//     #endif

//     #ifndef __EMSCRIPTEN_PTHREADS__
//         std::cout << "maybe enable pthreads" << std::endl;
//     #endif

//     if(!_flushthread.joinable()) _flushthread = std::thread(&JContainer::flushloop, this);
//        std::cout << "_needsFlush = true " << std::endl;

//     _needsFlush = true;
// };



