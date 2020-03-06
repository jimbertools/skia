#include <vector>
#include <string>
#include "SkSurface.h"
#include "SkCanvas.h"
#include "SkFont.h"
#include <thread>

#ifndef JCONTAINER
#define JCONTAINER

class JContainer;

typedef std::shared_ptr<JContainer> sharedJContainer;

class SK_API JContainer : public SkRefCnt {
    SkFont font = SkFont();
    std::thread _flushthread;
    bool _needsFlush;
    std::vector<sharedJContainer> _children = std::vector<sharedJContainer>();
    int _layerId;
    sharedJContainer _parent;
    sk_sp<GrContext> _context;
    sk_sp<SkSurface> _surface;
    SkCanvas* _canvas;
    SkPaint _paint;
    sharedJContainer _ref;

    SkScalar _x = 0;
    SkScalar _y = 0;
    SkScalar _width = 0;
    SkScalar _height = 0;

    public:
        JContainer(int layerId, sk_sp<GrContext> context);

        sharedJContainer ref();
        sk_sp<SkSurface> getSurface();
        sharedJContainer getParent();
        void removeFromParent();
        void removeAllChildren();
        void setParent(sharedJContainer parent);
        void addChild(sharedJContainer child);
        void removeChild(sharedJContainer child);
        void resize(SkScalar width, SkScalar height);
        SkScalar x();
        SkScalar y();
        void setX(SkScalar x);
        void setY(SkScalar y);
        int getLayerId();
        void draw(bool clear = false);

        void setNeedsFlush();
        void flushloop();
};

#endif
