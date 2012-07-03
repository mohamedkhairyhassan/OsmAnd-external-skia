/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "Simplify.h"

#undef SkASSERT
#define SkASSERT(cond) while (!(cond)) { sk_throw(); }

// Terminology:
// A Path contains one of more Contours
// A Contour is made up of Segment array
// A Segment is described by a Verb and a Point array with 2, 3, or 4 points
// A Verb is one of Line, Quad(ratic), or Cubic
// A Segment contains a Span array
// A Span is describes a portion of a Segment using starting and ending T
// T values range from 0 to 1, where 0 is the first Point in the Segment

// FIXME: remove once debugging is complete
#if 0 // set to 1 for no debugging whatsoever

//const bool gxRunTestsInOneThread = false;

#define DEBUG_ADD_INTERSECTING_TS 0
#define DEBUG_BRIDGE 0
#define DEBUG_CROSS 0
#define DEBUG_DUMP 0
#define DEBUG_PATH_CONSTRUCTION 0
#define DEBUG_WINDING 0
#define DEBUG_UNUSED 0 // set to expose unused functions
#define DEBUG_MARK_DONE 0

#else

//const bool gRunTestsInOneThread = true;

#define DEBUG_ADD_INTERSECTING_TS 0
#define DEBUG_BRIDGE 1
#define DEBUG_CROSS 1
#define DEBUG_DUMP 1
#define DEBUG_PATH_CONSTRUCTION 1
#define DEBUG_WINDING 0
#define DEBUG_UNUSED 0 // set to expose unused functions
#define DEBUG_MARK_DONE 0

#endif

#if DEBUG_DUMP
static const char* kLVerbStr[] = {"", "line", "quad", "cubic"};
// static const char* kUVerbStr[] = {"", "Line", "Quad", "Cubic"};
static int gContourID;
static int gSegmentID;
#endif

#ifndef DEBUG_TEST
#define DEBUG_TEST 0
#endif

static int LineIntersect(const SkPoint a[2], const SkPoint b[2],
        Intersections& intersections) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    const _Line bLine = {{b[0].fX, b[0].fY}, {b[1].fX, b[1].fY}};
    return intersect(aLine, bLine, intersections.fT[0], intersections.fT[1]);
}

static int QuadLineIntersect(const SkPoint a[3], const SkPoint b[2],
        Intersections& intersections) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    const _Line bLine = {{b[0].fX, b[0].fY}, {b[1].fX, b[1].fY}};
    intersect(aQuad, bLine, intersections);
    return intersections.fUsed;
}

static int CubicLineIntersect(const SkPoint a[2], const SkPoint b[3],
        Intersections& intersections) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    const _Line bLine = {{b[0].fX, b[0].fY}, {b[1].fX, b[1].fY}};
    return intersect(aCubic, bLine, intersections.fT[0], intersections.fT[1]);
}

static int QuadIntersect(const SkPoint a[3], const SkPoint b[3],
        Intersections& intersections) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    const Quadratic bQuad = {{b[0].fX, b[0].fY}, {b[1].fX, b[1].fY}, {b[2].fX, b[2].fY}};
    intersect(aQuad, bQuad, intersections);
    return intersections.fUsed;
}

static int CubicIntersect(const SkPoint a[4], const SkPoint b[4],
        Intersections& intersections) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    const Cubic bCubic = {{b[0].fX, b[0].fY}, {b[1].fX, b[1].fY}, {b[2].fX, b[2].fY},
            {b[3].fX, b[3].fY}};
    intersect(aCubic, bCubic, intersections);
    return intersections.fUsed;
}

static int HLineIntersect(const SkPoint a[2], SkScalar left, SkScalar right,
        SkScalar y, bool flipped, Intersections& intersections) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    return horizontalIntersect(aLine, left, right, y, flipped, intersections);
}

static int HQuadIntersect(const SkPoint a[3], SkScalar left, SkScalar right,
        SkScalar y, bool flipped, Intersections& intersections) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    return horizontalIntersect(aQuad, left, right, y, flipped, intersections);
}

static int HCubicIntersect(const SkPoint a[4], SkScalar left, SkScalar right,
        SkScalar y, bool flipped, Intersections& intersections) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    return horizontalIntersect(aCubic, left, right, y, flipped, intersections);
}

static int VLineIntersect(const SkPoint a[2], SkScalar top, SkScalar bottom,
        SkScalar x, bool flipped, Intersections& intersections) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    return verticalIntersect(aLine, top, bottom, x, flipped, intersections);
}

static int VQuadIntersect(const SkPoint a[3], SkScalar top, SkScalar bottom,
        SkScalar x, bool flipped, Intersections& intersections) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    return verticalIntersect(aQuad, top, bottom, x, flipped, intersections);
}

static int VCubicIntersect(const SkPoint a[4], SkScalar top, SkScalar bottom,
        SkScalar x, bool flipped, Intersections& intersections) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    return verticalIntersect(aCubic, top, bottom, x, flipped, intersections);
}

static int (* const VSegmentIntersect[])(const SkPoint [], SkScalar ,
        SkScalar , SkScalar , bool , Intersections& ) = {
    NULL,
    VLineIntersect,
    VQuadIntersect,
    VCubicIntersect
};

static void LineXYAtT(const SkPoint a[2], double t, SkPoint* out) {
    const _Line line = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    double x, y;
    xy_at_t(line, t, x, y);
    out->fX = SkDoubleToScalar(x);
    out->fY = SkDoubleToScalar(y);
}

static void QuadXYAtT(const SkPoint a[3], double t, SkPoint* out) {
    const Quadratic quad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    double x, y;
    xy_at_t(quad, t, x, y);
    out->fX = SkDoubleToScalar(x);
    out->fY = SkDoubleToScalar(y);
}

static void CubicXYAtT(const SkPoint a[4], double t, SkPoint* out) {
    const Cubic cubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    double x, y;
    xy_at_t(cubic, t, x, y);
    out->fX = SkDoubleToScalar(x);
    out->fY = SkDoubleToScalar(y);
}

static void (* const SegmentXYAtT[])(const SkPoint [], double , SkPoint* ) = {
    NULL,
    LineXYAtT,
    QuadXYAtT,
    CubicXYAtT
};

static SkScalar LineXAtT(const SkPoint a[2], double t) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    double x;
    xy_at_t(aLine, t, x, *(double*) 0);
    return SkDoubleToScalar(x);
}

static SkScalar QuadXAtT(const SkPoint a[3], double t) {
    const Quadratic quad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    double x;
    xy_at_t(quad, t, x, *(double*) 0);
    return SkDoubleToScalar(x);
}

static SkScalar CubicXAtT(const SkPoint a[4], double t) {
    const Cubic cubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    double x;
    xy_at_t(cubic, t, x, *(double*) 0);
    return SkDoubleToScalar(x);
}

static SkScalar (* const SegmentXAtT[])(const SkPoint [], double ) = {
    NULL,
    LineXAtT,
    QuadXAtT,
    CubicXAtT
};

static SkScalar LineYAtT(const SkPoint a[2], double t) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    double y;
    xy_at_t(aLine, t, *(double*) 0, y);
    return SkDoubleToScalar(y);
}

static SkScalar QuadYAtT(const SkPoint a[3], double t) {
    const Quadratic quad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    double y;
    xy_at_t(quad, t, *(double*) 0, y);
    return SkDoubleToScalar(y);
}

static SkScalar CubicYAtT(const SkPoint a[4], double t) {
    const Cubic cubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    double y;
    xy_at_t(cubic, t, *(double*) 0, y);
    return SkDoubleToScalar(y);
}

static SkScalar (* const SegmentYAtT[])(const SkPoint [], double ) = {
    NULL,
    LineYAtT,
    QuadYAtT,
    CubicYAtT
};

static SkScalar LineDXAtT(const SkPoint a[2], double ) {
    return a[1].fX - a[0].fX;
}

static SkScalar QuadDXAtT(const SkPoint a[3], double t) {
    const Quadratic quad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY}};
    double x;
    dxdy_at_t(quad, t, x, *(double*) 0);
    return SkDoubleToScalar(x);
}

static SkScalar CubicDXAtT(const SkPoint a[4], double t) {
    const Cubic cubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}, {a[2].fX, a[2].fY},
            {a[3].fX, a[3].fY}};
    double x;
    dxdy_at_t(cubic, t, x, *(double*) 0);
    return SkDoubleToScalar(x);
}

static SkScalar (* const SegmentDXAtT[])(const SkPoint [], double ) = {
    NULL,
    LineDXAtT,
    QuadDXAtT,
    CubicDXAtT
};

static void LineSubDivide(const SkPoint a[2], double startT, double endT,
        SkPoint sub[2]) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    _Line dst;
    sub_divide(aLine, startT, endT, dst);
    sub[0].fX = SkDoubleToScalar(dst[0].x);
    sub[0].fY = SkDoubleToScalar(dst[0].y);
    sub[1].fX = SkDoubleToScalar(dst[1].x);
    sub[1].fY = SkDoubleToScalar(dst[1].y);
}

static void QuadSubDivide(const SkPoint a[3], double startT, double endT,
        SkPoint sub[3]) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}};
    Quadratic dst;
    sub_divide(aQuad, startT, endT, dst);
    sub[0].fX = SkDoubleToScalar(dst[0].x);
    sub[0].fY = SkDoubleToScalar(dst[0].y);
    sub[1].fX = SkDoubleToScalar(dst[1].x);
    sub[1].fY = SkDoubleToScalar(dst[1].y);
    sub[2].fX = SkDoubleToScalar(dst[2].x);
    sub[2].fY = SkDoubleToScalar(dst[2].y);
}

static void CubicSubDivide(const SkPoint a[4], double startT, double endT,
        SkPoint sub[4]) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}, {a[3].fX, a[3].fY}};
    Cubic dst;
    sub_divide(aCubic, startT, endT, dst);
    sub[0].fX = SkDoubleToScalar(dst[0].x);
    sub[0].fY = SkDoubleToScalar(dst[0].y);
    sub[1].fX = SkDoubleToScalar(dst[1].x);
    sub[1].fY = SkDoubleToScalar(dst[1].y);
    sub[2].fX = SkDoubleToScalar(dst[2].x);
    sub[2].fY = SkDoubleToScalar(dst[2].y);
    sub[3].fX = SkDoubleToScalar(dst[3].x);
    sub[3].fY = SkDoubleToScalar(dst[3].y);
}

static void (* const SegmentSubDivide[])(const SkPoint [], double , double ,
        SkPoint []) = {
    NULL,
    LineSubDivide,
    QuadSubDivide,
    CubicSubDivide
};

#if DEBUG_UNUSED
static void QuadSubBounds(const SkPoint a[3], double startT, double endT,
        SkRect& bounds) {
    SkPoint dst[3];
    QuadSubDivide(a, startT, endT, dst);
    bounds.fLeft = bounds.fRight = dst[0].fX;
    bounds.fTop = bounds.fBottom = dst[0].fY;
    for (int index = 1; index < 3; ++index) {
        bounds.growToInclude(dst[index].fX, dst[index].fY);
    }
}

static void CubicSubBounds(const SkPoint a[4], double startT, double endT,
        SkRect& bounds) {
    SkPoint dst[4];
    CubicSubDivide(a, startT, endT, dst);
    bounds.fLeft = bounds.fRight = dst[0].fX;
    bounds.fTop = bounds.fBottom = dst[0].fY;
    for (int index = 1; index < 4; ++index) {
        bounds.growToInclude(dst[index].fX, dst[index].fY);
    }
}
#endif

static SkPath::Verb QuadReduceOrder(const SkPoint a[3],
        SkTDArray<SkPoint>& reducePts) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}};
    Quadratic dst;
    int order = reduceOrder(aQuad, dst);
    if (order == 3) {
        return SkPath::kQuad_Verb;
    }
    for (int index = 0; index < order; ++index) {
        SkPoint* pt = reducePts.append();
        pt->fX = SkDoubleToScalar(dst[index].x);
        pt->fY = SkDoubleToScalar(dst[index].y);
    }
    return (SkPath::Verb) (order - 1);
}

static SkPath::Verb CubicReduceOrder(const SkPoint a[4],
        SkTDArray<SkPoint>& reducePts) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}, {a[3].fX, a[3].fY}};
    Cubic dst;
    int order = reduceOrder(aCubic, dst, kReduceOrder_QuadraticsAllowed);
    if (order == 4) {
        return SkPath::kCubic_Verb;
    }
    for (int index = 0; index < order; ++index) {
        SkPoint* pt = reducePts.append();
        pt->fX = SkDoubleToScalar(dst[index].x);
        pt->fY = SkDoubleToScalar(dst[index].y);
    }
    return (SkPath::Verb) (order - 1);
}

static bool QuadIsLinear(const SkPoint a[3]) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}};
    return isLinear(aQuad, 0, 2);
}

static bool CubicIsLinear(const SkPoint a[4]) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}, {a[3].fX, a[3].fY}};
    return isLinear(aCubic, 0, 3);
}

static SkScalar LineLeftMost(const SkPoint a[2], double startT, double endT) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    double x[2];
    xy_at_t(aLine, startT, x[0], *(double*) 0);
    xy_at_t(aLine, endT, x[1], *(double*) 0);
    return SkMinScalar((float) x[0], (float) x[1]);
}

static SkScalar QuadLeftMost(const SkPoint a[3], double startT, double endT) {
    const Quadratic aQuad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}};
    return (float) leftMostT(aQuad, startT, endT);
}

static SkScalar CubicLeftMost(const SkPoint a[4], double startT, double endT) {
    const Cubic aCubic = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}, {a[3].fX, a[3].fY}};
    return (float) leftMostT(aCubic, startT, endT);
}

static SkScalar (* const SegmentLeftMost[])(const SkPoint [], double , double) = {
    NULL,
    LineLeftMost,
    QuadLeftMost,
    CubicLeftMost
};

#if DEBUG_UNUSED
static bool IsCoincident(const SkPoint a[2], const SkPoint& above,
        const SkPoint& below) {
    const _Line aLine = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY}};
    const _Line bLine = {{above.fX, above.fY}, {below.fX, below.fY}};
    return implicit_matches_ulps(aLine, bLine, 32);
}
#endif

class Segment;

// sorting angles
// given angles of {dx dy ddx ddy dddx dddy} sort them
class Angle {
public:
    // FIXME: this is bogus for quads and cubics
    // if the quads and cubics' line from end pt to ctrl pt are coincident,
    // there's no obvious way to determine the curve ordering from the
    // derivatives alone. In particular, if one quadratic's coincident tangent
    // is longer than the other curve, the final control point can place the
    // longer curve on either side of the shorter one.
    // Using Bezier curve focus http://cagd.cs.byu.edu/~tom/papers/bezclip.pdf
    // may provide some help, but nothing has been figured out yet.
    bool operator<(const Angle& rh) const {
        if ((fDy < 0) ^ (rh.fDy < 0)) {
            return fDy < 0;
        }
        if (fDy == 0 && rh.fDy == 0 && fDx != rh.fDx) {
            return fDx < rh.fDx;
        }
        SkScalar cmp = fDx * rh.fDy - rh.fDx * fDy;
        if (cmp) {
            return cmp < 0;
        }
        if ((fDDy < 0) ^ (rh.fDDy < 0)) {
            return fDDy < 0;
        }
        if (fDDy == 0 && rh.fDDy == 0 && fDDx != rh.fDDx) {
            return fDDx < rh.fDDx;
        }
        cmp = fDDx * rh.fDDy - rh.fDDx * fDDy;
        if (cmp) {
            return cmp < 0;
        }
        if ((fDDDy < 0) ^ (rh.fDDDy < 0)) {
            return fDDDy < 0;
        }
        if (fDDDy == 0 && rh.fDDDy == 0) {
            return fDDDx < rh.fDDDx;
        }
        return fDDDx * rh.fDDDy < rh.fDDDx * fDDDy;
    }

    bool cancels(const Angle& rh) const {
        return fDx * rh.fDx < 0 || fDy * rh.fDy < 0;
    }

    int end() const {
        return fEnd;
    }

    bool isHorizontal() const {
        return fDy == 0 && fDDy == 0 && fDDDy == 0;
    }

    // since all angles share a point, this needs to know which point
    // is the common origin, i.e., whether the center is at pts[0] or pts[verb]
    // practically, this should only be called by addAngle
    void set(const SkPoint* pts, SkPath::Verb verb, const Segment* segment,
            int start, int end) {
        SkASSERT(start != end);
        fSegment = segment;
        fStart = start;
        fEnd = end;
        fDx = pts[1].fX - pts[0].fX; // b - a
        fDy = pts[1].fY - pts[0].fY;
        if (verb == SkPath::kLine_Verb) {
            fDDx = fDDy = fDDDx = fDDDy = 0;
            return;
        }
        fDDx = pts[2].fX - pts[1].fX - fDx; // a - 2b + c
        fDDy = pts[2].fY - pts[1].fY - fDy;
        if (verb == SkPath::kQuad_Verb) {
            fDDDx = fDDDy = 0;
            return;
        }
        fDDDx = pts[3].fX + 3 * (pts[1].fX - pts[2].fX) - pts[0].fX;
        fDDDy = pts[3].fY + 3 * (pts[1].fY - pts[2].fY) - pts[0].fY;
    }

    // noncoincident quads/cubics may have the same initial angle
    // as lines, so must sort by derivatives as well
    // if flatness turns out to be a reasonable way to sort, use the below:
    void setFlat(const SkPoint* pts, SkPath::Verb verb, Segment* segment,
            int start, int end) {
        fSegment = segment;
        fStart = start;
        fEnd = end;
        fDx = pts[1].fX - pts[0].fX; // b - a
        fDy = pts[1].fY - pts[0].fY;
        if (verb == SkPath::kLine_Verb) {
            fDDx = fDDy = fDDDx = fDDDy = 0;
            return;
        }
        if (verb == SkPath::kQuad_Verb) {
            int uplsX = FloatAsInt(pts[2].fX - pts[1].fY - fDx);
            int uplsY = FloatAsInt(pts[2].fY - pts[1].fY - fDy);
            int larger = std::max(abs(uplsX), abs(uplsY));
            int shift = 0;
            double flatT;
            SkPoint ddPt; // FIXME: get rid of copy (change fDD_ to point)
            LineParameters implicitLine;
            _Line tangent = {{pts[0].fX, pts[0].fY}, {pts[1].fX, pts[1].fY}};
            implicitLine.lineEndPoints(tangent);
            implicitLine.normalize();
            while (larger > UlpsEpsilon * 1024) {
                larger >>= 2;
                ++shift;
                flatT = 0.5 / (1 << shift);
                QuadXYAtT(pts, flatT, &ddPt);
                _Point _pt = {ddPt.fX, ddPt.fY};
                double distance = implicitLine.pointDistance(_pt);
                if (approximately_zero(distance)) {
                    SkDebugf("%s ulps too small %1.9g\n", __FUNCTION__, distance);
                    break;
                }
            }
            flatT = 0.5 / (1 << shift);
            QuadXYAtT(pts, flatT, &ddPt);
            fDDx = ddPt.fX - pts[0].fX;
            fDDy = ddPt.fY - pts[0].fY;
            SkASSERT(fDDx != 0 || fDDy != 0);
            fDDDx = fDDDy = 0;
            return;
        }
        SkASSERT(0); // FIXME: add cubic case
    }
    
    Segment* segment() const {
        return const_cast<Segment*>(fSegment);
    }
    
    int sign() const {
        return SkSign32(fStart - fEnd);
    }
    
    int start() const {
        return fStart;
    }

private:
    SkScalar fDx;
    SkScalar fDy;
    SkScalar fDDx;
    SkScalar fDDy;
    SkScalar fDDDx;
    SkScalar fDDDy;
    const Segment* fSegment;
    int fStart;
    int fEnd;
};

static void sortAngles(SkTDArray<Angle>& angles, SkTDArray<Angle*>& angleList) {
    int angleCount = angles.count();
    int angleIndex;
    angleList.setReserve(angleCount);
    for (angleIndex = 0; angleIndex < angleCount; ++angleIndex) {
        *angleList.append() = &angles[angleIndex];
    }
    QSort<Angle>(angleList.begin(), angleList.end() - 1);
}

// Bounds, unlike Rect, does not consider a line to be empty.
struct Bounds : public SkRect {
    static bool Intersects(const Bounds& a, const Bounds& b) {
        return a.fLeft <= b.fRight && b.fLeft <= a.fRight &&
                a.fTop <= b.fBottom && b.fTop <= a.fBottom;
    }

    void add(SkScalar left, SkScalar top, SkScalar right, SkScalar bottom) {
        if (left < fLeft) {
            fLeft = left;
        }
        if (top < fTop) {
            fTop = top;
        }
        if (right > fRight) {
            fRight = right;
        }
        if (bottom > fBottom) {
            fBottom = bottom;
        }
    }

    void add(const Bounds& toAdd) {
        add(toAdd.fLeft, toAdd.fTop, toAdd.fRight, toAdd.fBottom);
    }

    bool isEmpty() {
        return fLeft > fRight || fTop > fBottom
                || fLeft == fRight && fTop == fBottom
                || isnan(fLeft) || isnan(fRight)
                || isnan(fTop) || isnan(fBottom);
    }

    void setCubicBounds(const SkPoint a[4]) {
        _Rect dRect;
        Cubic cubic  = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
            {a[2].fX, a[2].fY}, {a[3].fX, a[3].fY}};
        dRect.setBounds(cubic);
        set((float) dRect.left, (float) dRect.top, (float) dRect.right,
                (float) dRect.bottom);
    }

    void setQuadBounds(const SkPoint a[3]) {
        const Quadratic quad = {{a[0].fX, a[0].fY}, {a[1].fX, a[1].fY},
                {a[2].fX, a[2].fY}};
        _Rect dRect;
        dRect.setBounds(quad);
        set((float) dRect.left, (float) dRect.top, (float) dRect.right,
                (float) dRect.bottom);
    }
};

struct Span {
    Segment* fOther;
    mutable SkPoint const* fPt; // lazily computed as needed
    double fT;
    double fOtherT; // value at fOther[fOtherIndex].fT
    int fOtherIndex;  // can't be used during intersection
    int fWindSum; // accumulated from contours surrounding this one
    int fWindValue; // 0 == canceled; 1 == normal; >1 == coincident
    bool fDone; // if set, this span to next higher T has been processed
};

class Segment {
public:
    Segment() {
#if DEBUG_DUMP
        fID = ++gSegmentID;
#endif
    }
    
    SkScalar activeTop() const {
        SkASSERT(!done());
        int count = fTs.count();
        SkScalar result = SK_ScalarMax;
        bool lastDone = true;
        for (int index = 0; index < count; ++index) {
            bool done = fTs[index].fDone;
            if (!done || !lastDone) {
                SkScalar y = yAtT(index);
                if (result > y) {
                    result = y;
                }
            }
            lastDone = done;
        }
        SkASSERT(result < SK_ScalarMax);
        return result;
    }

    void addAngle(SkTDArray<Angle>& angles, int start, int end) const {
        SkASSERT(start != end);
        SkPoint edge[4];
        (*SegmentSubDivide[fVerb])(fPts, fTs[start].fT, fTs[end].fT, edge);
        Angle* angle = angles.append();
        angle->set(edge, fVerb, this, start, end);
    }

    void addCubic(const SkPoint pts[4]) {
        init(pts, SkPath::kCubic_Verb);
        fBounds.setCubicBounds(pts);
    }

    // FIXME: this needs to defer add for aligned consecutive line segments
    SkPoint addCurveTo(int start, int end, SkPath& path, bool active) {
        SkPoint edge[4];
        // OPTIMIZE? if not active, skip remainder and return xy_at_t(end)
        (*SegmentSubDivide[fVerb])(fPts, fTs[start].fT, fTs[end].fT, edge);
        if (active) {
    #if DEBUG_PATH_CONSTRUCTION
            SkDebugf("%s %s (%1.9g,%1.9g)", __FUNCTION__,
                    kLVerbStr[fVerb], edge[1].fX, edge[1].fY);
            if (fVerb > 1) {
                SkDebugf(" (%1.9g,%1.9g)", edge[2].fX, edge[2].fY);
            }
            if (fVerb > 2) {
                SkDebugf(" (%1.9g,%1.9g)", edge[3].fX, edge[3].fY);
            }
            SkDebugf("\n");
    #endif
            switch (fVerb) {
                case SkPath::kLine_Verb:
                    path.lineTo(edge[1].fX, edge[1].fY);
                    break;
                case SkPath::kQuad_Verb:
                    path.quadTo(edge[1].fX, edge[1].fY, edge[2].fX, edge[2].fY);
                    break;
                case SkPath::kCubic_Verb:
                    path.cubicTo(edge[1].fX, edge[1].fY, edge[2].fX, edge[2].fY,
                            edge[3].fX, edge[3].fY);
                    break;
            }
        }
        return edge[fVerb];
    }

    void addLine(const SkPoint pts[2]) {
        init(pts, SkPath::kLine_Verb);
        fBounds.set(pts, 2);
    }

    const SkPoint& addMoveTo(int tIndex, SkPath& path, bool active) {
        const SkPoint& pt = xyAtT(tIndex);
        if (active) {
    #if DEBUG_PATH_CONSTRUCTION
            SkDebugf("%s (%1.9g,%1.9g)\n", __FUNCTION__, pt.fX, pt.fY);
    #endif
            path.moveTo(pt.fX, pt.fY);
        }
        return pt;
    }

    // add 2 to edge or out of range values to get T extremes
    void addOtherT(int index, double otherT, int otherIndex) {
        Span& span = fTs[index];
        span.fOtherT = otherT;
        span.fOtherIndex = otherIndex;
    }

    void addQuad(const SkPoint pts[3]) {
        init(pts, SkPath::kQuad_Verb);
        fBounds.setQuadBounds(pts);
    }
    
    // Defer all coincident edge processing until
    // after normal intersections have been computed

// no need to be tricky; insert in normal T order
// resolve overlapping ts when considering coincidence later

    // add non-coincident intersection. Resulting edges are sorted in T.
    int addT(double newT, Segment* other) {
        // FIXME: in the pathological case where there is a ton of intercepts,
        //  binary search?
        int insertedAt = -1;
        size_t tCount = fTs.count();
        for (size_t index = 0; index < tCount; ++index) {
            // OPTIMIZATION: if there are three or more identical Ts, then
            // the fourth and following could be further insertion-sorted so
            // that all the edges are clockwise or counterclockwise.
            // This could later limit segment tests to the two adjacent
            // neighbors, although it doesn't help with determining which
            // circular direction to go in.
            if (newT < fTs[index].fT) {
                insertedAt = index;
                break;
            }
        }
        Span* span;
        if (insertedAt >= 0) {
            span = fTs.insert(insertedAt);
        } else {
            insertedAt = tCount;
            span = fTs.append();
        }
        span->fT = newT;
        span->fOther = other;
        span->fPt = NULL;
        span->fWindSum = SK_MinS32;
        span->fWindValue = 1;
        if ((span->fDone = newT == 1)) {
            ++fDoneSpans;
        } 
        return insertedAt;
    }

    // set spans from start to end to decrement by one
    // note this walks other backwards
    // FIMXE: there's probably an edge case that can be constructed where
    // two span in one segment are separated by float epsilon on one span but
    // not the other, if one segment is very small. For this
    // case the counts asserted below may or may not be enough to separate the
    // spans. Even if the counts work out, what if the spanw aren't correctly
    // sorted? It feels better in such a case to match the span's other span
    // pointer since both coincident segments must contain the same spans.
    void addTCancel(double startT, double endT, Segment& other,
            double oStartT, double oEndT) {
        SkASSERT(endT - startT >= FLT_EPSILON);
        SkASSERT(oEndT - oStartT >= FLT_EPSILON);
        int index = 0;
        while (startT - fTs[index].fT >= FLT_EPSILON) {
            ++index;
        }
        int oCount = other.fTs.count();
        while (other.fTs[--oCount].fT - oEndT >= FLT_EPSILON)
            ;
        int oIndex = oCount;
        while (other.fTs[--oIndex].fT - oEndT > -FLT_EPSILON)
            ;
        Span* test = &fTs[index];
        Span* oTest = &other.fTs[oIndex];
        SkDEBUGCODE(int testWindValue = test->fWindValue);
        SkDEBUGCODE(int oTestWindValue = oTest->fWindValue);
        SkDEBUGCODE(int startIndex = index);
        do {
            bool decrement = test->fWindValue && oTest->fWindValue;
            Span* end = test;
            do {
                SkASSERT(testWindValue == end->fWindValue);
                if (decrement) {
                    if (--(end->fWindValue) == 0) {
                        end->fDone = true;
                        ++fDoneSpans;
                    }
                }
                end = &fTs[++index];
            } while (end->fT - test->fT < FLT_EPSILON);
            SkASSERT(oCount - oIndex == index - startIndex);
            Span* oTestStart = oTest;
            SkDEBUGCODE(oCount = oIndex);
            do {
                SkASSERT(oTestWindValue == oTestStart->fWindValue);
                if (decrement) {
                    if (--(oTestStart->fWindValue) == 0) {
                        oTestStart->fDone = true;
                        ++other.fDoneSpans;
                    }
                }
                if (!oIndex) {
                    break;
                }
                oTestStart = &other.fTs[--oIndex];
            } while (oTest->fT - oTestStart->fT < FLT_EPSILON); 
            test = end;
            oTest = oTestStart;
        } while (test->fT < endT - FLT_EPSILON);
        SkASSERT(!oIndex || oTest->fT <= oStartT - FLT_EPSILON);
    }

    // set spans from start to end to increment the greater by one and decrement
    // the lesser
    void addTCoincident(double startT, double endT, Segment& other,
            double oStartT, double oEndT) {
        SkASSERT(endT - startT >= FLT_EPSILON);
        SkASSERT(oEndT - oStartT >= FLT_EPSILON);
        int index = 0;
        while (startT - fTs[index].fT >= FLT_EPSILON) {
            ++index;
        }
        int oIndex = 0;
        while (oStartT - other.fTs[oIndex].fT >= FLT_EPSILON) {
            ++oIndex;
        }
        Span* test = &fTs[index];
        Span* oTest = &other.fTs[oIndex];
        SkDEBUGCODE(int testWindValue = test->fWindValue);
        SkDEBUGCODE(int oTestWindValue = oTest->fWindValue);
        SkTDArray<double> outsideTs;
        SkTDArray<double> oOutsideTs;
        do {
            bool decrementOther = test->fWindValue >= oTest->fWindValue;
            Span* end = test;
            double startT = end->fT;
            double oStartT = oTest->fT;
            do {
                SkASSERT(testWindValue == end->fWindValue);
                if (decrementOther) {
                    ++(end->fWindValue);
                } else {
                    if (--(end->fWindValue) == 0) {
                        end->fDone = true;
                        ++fDoneSpans;
                        *outsideTs.append() = end->fT;
                        *outsideTs.append() = oStartT;
                    }
                }
                end = &fTs[++index];
            } while (end->fT - test->fT < FLT_EPSILON);
            Span* oEnd = oTest;
            do {
                SkASSERT(oTestWindValue == oEnd->fWindValue);
                if (decrementOther) {
                    if (--(oEnd->fWindValue) == 0) {
                        oEnd->fDone = true;
                        ++other.fDoneSpans;
                        *oOutsideTs.append() = oEnd->fT;
                        *oOutsideTs.append() = startT;
                    }
                } else {
                    ++(oEnd->fWindValue);
                }
                oEnd = &other.fTs[++oIndex];
            } while (oEnd->fT - oTest->fT < FLT_EPSILON);
            test = end;
            oTest = oEnd;
        } while (test->fT < endT - FLT_EPSILON);
        SkASSERT(oTest->fT < oEndT + FLT_EPSILON);
        SkASSERT(oTest->fT > oEndT - FLT_EPSILON);
        if (!done() && outsideTs.count()) {
            addTOutsides(outsideTs, &other, oEndT);
        }
        if (!other.done() && oOutsideTs.count()) {
            other.addTOutsides(oOutsideTs, this, endT);
        }
    }

    void addTOutsides(const SkTDArray<double>& outsideTs, Segment* other,
            double otherEnd) {
        int count = outsideTs.count();
        double endT = 0;
        int endSpan = 0;
        for (int index = 0; index < count; index += 2) {
            double t = outsideTs[index];
            double otherT = outsideTs[index + 1];
            if (t > 1 - FLT_EPSILON) {
                return;
            }
            if (t - endT > FLT_EPSILON) {
                endSpan = addTPair(t, other, otherT);
            }
            do {
                endT = fTs[++endSpan].fT;
            } while (endT - t < FLT_EPSILON);
        }
        addTPair(endT, other, otherEnd);
    }
    
    int addTPair(double t, Segment* other, double otherT) {
        int insertedAt = addT(t, other);
        int otherInsertedAt = other->addT(otherT, this);
        addOtherT(insertedAt, otherT, otherInsertedAt);
        other->addOtherT(otherInsertedAt, t, insertedAt);
        return insertedAt;
    }

    void addTwoAngles(int start, int end, SkTDArray<Angle>& angles) const {
        // add edge leading into junction
        if (fTs[SkMin32(end, start)].fWindValue > 0) {
            addAngle(angles, end, start);
        }
        // add edge leading away from junction
        int step = SkSign32(end - start);
        int tIndex = nextSpan(end, step);
        if (tIndex >= 0 && fTs[SkMin32(end, tIndex)].fWindValue > 0) {
            addAngle(angles, end, tIndex);
        }
    }

    const Bounds& bounds() const {
        return fBounds;
    }

    void buildAngles(int index, SkTDArray<Angle>& angles) const {
        double referenceT = fTs[index].fT;
        int lesser = index;
        while (--lesser >= 0 && referenceT - fTs[lesser].fT < FLT_EPSILON) {
            buildAnglesInner(lesser, angles);
        }
        do {
            buildAnglesInner(index, angles);
        } while (++index < fTs.count() && fTs[index].fT - referenceT < FLT_EPSILON);
    }

    void buildAnglesInner(int index, SkTDArray<Angle>& angles) const {
        Span* span = &fTs[index];
        Segment* other = span->fOther;
    // if there is only one live crossing, and no coincidence, continue
    // in the same direction
    // if there is coincidence, the only choice may be to reverse direction
        // find edge on either side of intersection
        int oIndex = span->fOtherIndex;
        // if done == -1, prior span has already been processed
        int step = 1;
        int next = other->nextSpan(oIndex, step);
        if (next < 0) {
            step = -step;
            next = other->nextSpan(oIndex, step);
        }
        // add candidate into and away from junction
        other->addTwoAngles(next, oIndex, angles);
    }

    // OPTIMIZATION: inefficient, refactor
    bool cancels(const Segment& other) const {
        SkTDArray<Angle> angles;
        addAngle(angles, 0, fTs.count() - 1);
        other.addAngle(angles, 0, other.fTs.count() - 1);
        return angles[0].cancels(angles[1]);
    }

    // figure out if the segment's ascending T goes clockwise or not
    // not enough context to write this as shown
    // instead, add all segments meeting at the top
    // sort them using buildAngleList
    // find the first in the sort
    // see if ascendingT goes to top
    bool clockwise(int /* tIndex */) const {
        SkASSERT(0); // incomplete
        return false;
    }

    int crossedSpan(const SkPoint& basePt, SkScalar& bestY, double& hitT) const {
        int start = 0;
        int bestT = -1;
        SkScalar top = bounds().fTop;
        SkScalar bottom = bounds().fBottom;
        int end;
        do {
            end = nextSpan(start, 1);
            SkPoint edge[4];
            // OPTIMIZE: wrap this so that if start==0 end==fTCount-1 we can 
            // work with the original data directly
            (*SegmentSubDivide[fVerb])(fPts, fTs[start].fT, fTs[end].fT, edge);
            // start here; intersect ray starting at basePt with edge
            Intersections intersections;
            int pts = (*VSegmentIntersect[fVerb])(edge, top, bottom, basePt.fX,
                    false, intersections);
            if (pts == 0) {
                continue;
            }
            if (pts > 1 && fVerb == SkPath::kLine_Verb) {
            // if the intersection is edge on, wait for another one
                continue;
            }
            SkASSERT(pts == 1); // FIXME: more code required to disambiguate
            SkPoint pt;
            double foundT = intersections.fT[0][0];
            (*SegmentXYAtT[fVerb])(fPts, foundT, &pt);
            if (bestY < pt.fY) {
                bestY = pt.fY;
                bestT = foundT < 1 ? start : end;
                hitT = foundT;
            }
            start = end;
        } while (fTs[end].fT != 1);
        return bestT;
    }
        
    bool done() const {
        SkASSERT(fDoneSpans <= fTs.count());
        return fDoneSpans == fTs.count();
    }

    // so the span needs to contain the pairing info found here
    // this should include the winding computed for the edge, and
    //  what edge it connects to, and whether it is discarded
    //  (maybe discarded == abs(winding) > 1) ?
    // only need derivatives for duration of sorting, add a new struct
    // for pairings, remove extra spans that have zero length and
    //  reference an unused other
    // for coincident, the last span on the other may be marked done
    //  (always?)
    
    // if loop is exhausted, contour may be closed.
    // FIXME: pass in close point so we can check for closure

    // given a segment, and a sense of where 'inside' is, return the next
    // segment. If this segment has an intersection, or ends in multiple
    // segments, find the mate that continues the outside.
    // note that if there are multiples, but no coincidence, we can limit
    // choices to connections in the correct direction
    
    // mark found segments as done

    // start is the index of the beginning T of this edge
    // it is guaranteed to have an end which describes a non-zero length (?)
    // winding -1 means ccw, 1 means cw
    // firstFind allows coincident edges to be treated differently
    Segment* findNext(int winding, const int startIndex, const int endIndex,
            int& nextStart, int& nextEnd, bool firstFind) {
        SkASSERT(startIndex != endIndex);
        int count = fTs.count();
        SkASSERT(startIndex < endIndex ? startIndex < count - 1
                : startIndex > 0);
        int step = SkSign32(endIndex - startIndex);
        int end = nextSpan(startIndex, step);
        SkASSERT(end >= 0);
        Span* endSpan = &fTs[end];
        Segment* other;
        if (isSimple(end)) {
        // mark the smaller of startIndex, endIndex done, and all adjacent
        // spans with the same T value (but not 'other' spans)
            markDone(SkMin32(startIndex, endIndex), winding);
            other = endSpan->fOther;
            nextStart = endSpan->fOtherIndex;
            nextEnd = nextStart + step;
            SkASSERT(step < 0 ? nextEnd >= 0 : nextEnd < other->fTs.count());
            return other;
        }
        // more than one viable candidate -- measure angles to find best
        SkTDArray<Angle> angles;
        SkASSERT(startIndex - endIndex != 0);
        SkASSERT((startIndex - endIndex < 0) ^ (step < 0));
        addTwoAngles(startIndex, end, angles);
        buildAngles(end, angles);
        SkTDArray<Angle*> sorted;
        sortAngles(angles, sorted);
        // find the starting edge
        int firstIndex = -1;
        int angleCount = angles.count();
        int angleIndex;
        const Angle* angle;
        for (angleIndex = 0; angleIndex < angleCount; ++angleIndex) {
            angle = sorted[angleIndex];
            if (angle->segment() == this && angle->start() == end &&
                    angle->end() == startIndex) {
                firstIndex = angleIndex;
                break;
            }
        }
        // back up if prior edge is coincident with firstIndex
   //     adjustFirst(sorted, firstIndex, winding, firstFind);
        SkASSERT(firstIndex >= 0);
        int startWinding = winding;
        int nextIndex = firstIndex + 1;
        int lastIndex = firstIndex != 0 ? firstIndex : angleCount;
        const Angle* foundAngle = NULL;
  //      bool alreadyMarked = angle->segment()->fTs[SkMin32(angle->start(),
  //              angle->end())].fDone;
        // iterate through the angle, and compute everyone's winding
        bool firstEdge = true;
        do {
            if (nextIndex == angleCount) {
                nextIndex = 0;
            }
            const Angle* nextAngle = sorted[nextIndex];
            int maxWinding = winding;
            Segment* nextSegment = nextAngle->segment();
            int windValue = nextSegment->windValue(nextAngle);
            SkASSERT(windValue > 0);
            winding -= nextAngle->sign() * windValue;
            firstEdge = false;
            if (!winding) {
                if (!foundAngle) {
                    foundAngle = nextAngle;
                }
                goto doNext;
            }
            if (nextSegment->done()) {
                goto doNext;
            }
            // if the winding is non-zero, nextAngle does not connect to
            // current chain. If we haven't done so already, mark the angle
            // as done, record the winding value, and mark connected unambiguous
            // segments as well.
            if (nextSegment->winding(nextAngle) == SK_MinS32) {
                if (abs(maxWinding) < abs(winding)) {
                    maxWinding = winding;
                }
                if (foundAngle) {
                    nextSegment->markAndChaseWinding(nextAngle, maxWinding);
                } else {
                    nextSegment->markAndChaseDone(nextAngle, maxWinding);
                }
            }
    doNext:
            angle = nextAngle;
        } while (++nextIndex != lastIndex);
   //     if (!alreadyMarked) {
            sorted[firstIndex]->segment()->
                markDone(SkMin32(startIndex, endIndex), startWinding);
   //     }
        if (!foundAngle) {
            return NULL;
        }
        nextStart = foundAngle->start();
        nextEnd = foundAngle->end();
        return foundAngle->segment();
    }
    
    // FIXME: this is tricky code; needs its own unit test
    void findTooCloseToCall(int /* winding */ ) { // FIXME: winding should be considered
        int count = fTs.count();
        if (count < 3) { // require t=0, x, 1 at minimum
            return;
        }
        int matchIndex = 0;
        int moCount;
        Span* match;
        Segment* mOther;
        do {
            match = &fTs[matchIndex];
            mOther = match->fOther;
            moCount = mOther->fTs.count();
            if (moCount >= 3) {
                break;
            }
            if (++matchIndex >= count) {
                return;
            }
        } while (true); // require t=0, x, 1 at minimum
        // OPTIMIZATION: defer matchPt until qualifying toCount is found?
        const SkPoint* matchPt = &xyAtT(match);
        // look for a pair of nearby T values that map to the same (x,y) value
        // if found, see if the pair of other segments share a common point. If
        // so, the span from here to there is coincident.
        for (int index = matchIndex + 1; index < count; ++index) {
            Span* test = &fTs[index];
            if (test->fDone) {
                continue;
            }
            Segment* tOther = test->fOther;
            int toCount = tOther->fTs.count();
            if (toCount < 3) { // require t=0, x, 1 at minimum
                continue;
            }
            const SkPoint* testPt = &xyAtT(test);
            if (*matchPt != *testPt) {
                matchIndex = index;
                moCount = toCount;
                match = test;
                mOther = tOther;
                matchPt = testPt;
                continue;
            }
            int moStart = -1;
            int moEnd = -1;
            double moStartT, moEndT;
            for (int moIndex = 0; moIndex < moCount; ++moIndex) {
                Span& moSpan = mOther->fTs[moIndex];
                if (moSpan.fDone) {
                    continue;
                }
                if (moSpan.fOther == this) {
                    if (moSpan.fOtherT == match->fT) {
                        moStart = moIndex;
                        moStartT = moSpan.fT;
                    }
                    continue;
                }
                if (moSpan.fOther == tOther) {
                    SkASSERT(moEnd == -1);
                    moEnd = moIndex;
                    moEndT = moSpan.fT;
                }
            }
            if (moStart < 0 || moEnd < 0) {
                continue;
            }
            // FIXME: if moStartT, moEndT are initialized to NaN, can skip this test
            if (moStartT == moEndT) {
                continue;
            }
            int toStart = -1;
            int toEnd = -1;
            double toStartT, toEndT;
            for (int toIndex = 0; toIndex < toCount; ++toIndex) {
                Span& toSpan = tOther->fTs[toIndex];
                if (toSpan.fOther == this) {
                    if (toSpan.fOtherT == test->fT) {
                        toStart = toIndex;
                        toStartT = toSpan.fT;
                    }
                    continue;
                }
                if (toSpan.fOther == mOther && toSpan.fOtherT == moEndT) {
                    SkASSERT(toEnd == -1);
                    toEnd = toIndex;
                    toEndT = toSpan.fT;
                }
            }
            // FIXME: if toStartT, toEndT are initialized to NaN, can skip this test
            if (toStart <= 0 || toEnd <= 0) {
                continue;
            }
            if (toStartT == toEndT) {
                continue;
            }
            // test to see if the segment between there and here is linear
            if (!mOther->isLinear(moStart, moEnd)
                    || !tOther->isLinear(toStart, toEnd)) {
                continue;
            }
            // FIXME: defer implementation until the rest works
            // this may share code with regular coincident detection
            SkASSERT(0);
        #if 0
            if (flipped) {
                mOther->addTCancel(moStart, moEnd, tOther, tStart, tEnd);
            } else {
                mOther->addTCoincident(moStart, moEnd, tOther, tStart, tEnd);
            }
        #endif
        }
    }

    // OPTIMIZATION : for a pair of lines, can we compute points at T (cached)
    // and use more concise logic like the old edge walker code?
    // FIXME: this needs to deal with coincident edges
    Segment* findTop(int& tIndex, int& endIndex) {
        // iterate through T intersections and return topmost
        // topmost tangent from y-min to first pt is closer to horizontal
        SkASSERT(!done());
        int firstT;
        int lastT;
        SkPoint topPt;
        topPt.fY = SK_ScalarMax;
        int count = fTs.count();
        // see if either end is not done since we want smaller Y of the pair
        bool lastDone = true;
        for (int index = 0; index < count; ++index) {
            const Span& span = fTs[index];
            if (!span.fDone || !lastDone) {
                const SkPoint& intercept = xyAtT(&span);
                if (topPt.fY > intercept.fY || (topPt.fY == intercept.fY
                        && topPt.fX > intercept.fX)) {
                    topPt = intercept;
                    firstT = lastT = index;
                } else if (topPt == intercept) {
                    lastT = index;
                }
            }
            lastDone = span.fDone;
        }
        // sort the edges to find the leftmost
        int step = 1;
        int end = nextSpan(firstT, step);
        if (end == -1) {
            step = -1;
            end = nextSpan(firstT, step);
            SkASSERT(end != -1);
        }
        // if the topmost T is not on end, or is three-way or more, find left
        // look for left-ness from tLeft to firstT (matching y of other)
        SkTDArray<Angle> angles;
        SkASSERT(firstT - end != 0);
        addTwoAngles(end, firstT, angles);
        buildAngles(firstT, angles);
        SkTDArray<Angle*> sorted;
        sortAngles(angles, sorted);
        // skip edges that have already been processed
        firstT = -1;
        Segment* leftSegment;
        do {
            const Angle* angle = sorted[++firstT];
            leftSegment = angle->segment();
            tIndex = angle->end();
            endIndex = angle->start();
        } while (leftSegment->fTs[SkMin32(tIndex, endIndex)].fDone);
        return leftSegment;
    }

    // FIXME: not crazy about this
    // when the intersections are performed, the other index is into an
    // incomplete array. as the array grows, the indices become incorrect
    // while the following fixes the indices up again, it isn't smart about
    // skipping segments whose indices are already correct
    // assuming we leave the code that wrote the index in the first place
    void fixOtherTIndex() {
        int iCount = fTs.count();
        for (int i = 0; i < iCount; ++i) {
            Span& iSpan = fTs[i];
            double oT = iSpan.fOtherT;
            Segment* other = iSpan.fOther;
            int oCount = other->fTs.count();
            for (int o = 0; o < oCount; ++o) {
                Span& oSpan = other->fTs[o];
                if (oT == oSpan.fT && this == oSpan.fOther) {
                    iSpan.fOtherIndex = o;
                    break;
                }
            }
        }
    }
    
    // OPTIMIZATION: uses tail recursion. Unwise?
    void innerChaseDone(int index, int step, int winding) {
        int end = nextSpan(index, step);
        if (multipleSpans(end, step)) {
            return;
        }
        const Span& endSpan = fTs[end];
        Segment* other = endSpan.fOther;
        index = endSpan.fOtherIndex;
        int otherEnd = other->nextSpan(index, step);
        other->innerChaseDone(index, step, winding);
        other->markDone(SkMin32(index, otherEnd), winding);
    }
    
    void innerChaseWinding(int index, int step, int winding) {
        int end = nextSpan(index, step);
        if (multipleSpans(end, step)) {
            return;
        }
        const Span& endSpan = fTs[end];
        Segment* other = endSpan.fOther;
        index = endSpan.fOtherIndex;
        int otherEnd = other->nextSpan(index, step);
        int min = SkMin32(index, otherEnd);
        if (other->fTs[min].fWindSum != SK_MinS32) {
            SkASSERT(other->fTs[index].fWindSum == winding);
            return;
        }
        other->innerChaseWinding(index, step, winding);
        other->markWinding(min, winding);
    }
    
    void init(const SkPoint pts[], SkPath::Verb verb) {
        fPts = pts;
        fVerb = verb;
        fDoneSpans = 0;
    }

    bool intersected() const {
        return fTs.count() > 0;
    }
    
    bool isLinear(int start, int end) const {
        if (fVerb == SkPath::kLine_Verb) {
            return true;
        }
        if (fVerb == SkPath::kQuad_Verb) {
            SkPoint qPart[3];
            QuadSubDivide(fPts, fTs[start].fT, fTs[end].fT, qPart);
            return QuadIsLinear(qPart);
        } else {
            SkASSERT(fVerb == SkPath::kCubic_Verb);
            SkPoint cPart[4];
            CubicSubDivide(fPts, fTs[start].fT, fTs[end].fT, cPart);
            return CubicIsLinear(cPart);
        }
    }
    
    bool isSimple(int end) const {
        int count = fTs.count();
        if (count == 2) {
            return true;
        }
        double t = fTs[end].fT;
        if (t < FLT_EPSILON) {
            return fTs[1].fT >= FLT_EPSILON;
        }
        if (t > 1 - FLT_EPSILON) {
            return fTs[count - 2].fT <= 1 - FLT_EPSILON;
        }
        return false;
    }

    bool isHorizontal() const {
        return fBounds.fTop == fBounds.fBottom;
    }

    bool isVertical() const {
        return fBounds.fLeft == fBounds.fRight;
    }

    SkScalar leftMost(int start, int end) const {
        return (*SegmentLeftMost[fVerb])(fPts, fTs[start].fT, fTs[end].fT);
    }
    
    // this span is excluded by the winding rule -- chase the ends
    // as long as they are unambiguous to mark connections as done
    // and give them the same winding value
    void markAndChaseDone(const Angle* angle, int winding) {
        int index = angle->start();
        int endIndex = angle->end();
        int step = SkSign32(endIndex - index);
        innerChaseDone(index, step, winding);
        markDone(SkMin32(index, endIndex), winding);
    }
    
    void markAndChaseWinding(const Angle* angle, int winding) {
        int index = angle->start();
        int endIndex = angle->end();
        int min = SkMin32(index, endIndex);
        int step = SkSign32(endIndex - index);
        innerChaseWinding(index, step, winding);
        markWinding(min, winding);
    }
    
    // FIXME: this should also mark spans with equal (x,y)
    // This may be called when the segment is already marked done. While this
    // wastes time, it shouldn't do any more than spin through the T spans.
    // OPTIMIZATION: abort on first done found (assuming that this code is 
    // always called to mark segments done).
    void markDone(int index, int winding) {
      //  SkASSERT(!done());
        double referenceT = fTs[index].fT;
        int lesser = index;
        while (--lesser >= 0 && referenceT - fTs[lesser].fT < FLT_EPSILON) {
            Span& span = fTs[lesser];
            if (span.fDone) {
                continue;
            }
        #if DEBUG_MARK_DONE
            const SkPoint& pt = xyAtT(&span);
            SkDebugf("%s segment=%d index=%d t=%1.9g pt=(%1.9g,%1.9g) wind=%d\n",
                    __FUNCTION__, fID, lesser, span.fT, pt.fX, pt.fY, winding);
        #endif
            span.fDone = true;
            SkASSERT(span.fWindSum == SK_MinS32 || span.fWindSum == winding);
            span.fWindSum = winding;
            fDoneSpans++;
        }
        do {
            Span& span = fTs[index];
     //       SkASSERT(!span.fDone);
            if (span.fDone) {
                continue;
            }
        #if DEBUG_MARK_DONE
            const SkPoint& pt = xyAtT(&span);
            SkDebugf("%s segment=%d index=%d t=%1.9g pt=(%1.9g,%1.9g) wind=%d\n",
                    __FUNCTION__, fID, index, span.fT, pt.fX, pt.fY, winding);
        #endif
            span.fDone = true;
            SkASSERT(span.fWindSum == SK_MinS32 || span.fWindSum == winding);
            span.fWindSum = winding;
            fDoneSpans++;
        } while (++index < fTs.count() && fTs[index].fT - referenceT < FLT_EPSILON);
    }

    void markWinding(int index, int winding) {
        SkASSERT(!done());
        double referenceT = fTs[index].fT;
        int lesser = index;
        while (--lesser >= 0 && referenceT - fTs[lesser].fT < FLT_EPSILON) {
            Span& span = fTs[lesser];
            if (span.fDone) {
                continue;
            }
            SkASSERT(span.fWindValue == 1 || winding == 0);
            SkASSERT(span.fWindSum == SK_MinS32 || span.fWindSum == winding);
        #if DEBUG_MARK_DONE
            const SkPoint& pt = xyAtT(&span);
            SkDebugf("%s segment=%d index=%d t=%1.9g pt=(%1.9g,%1.9g) wind=%d\n",
                    __FUNCTION__, fID, lesser, span.fT, pt.fX, pt.fY, winding);
        #endif
            span.fWindSum = winding;
        }
        do {
            Span& span = fTs[index];
     //       SkASSERT(!span.fDone || span.fCoincident);
            if (span.fDone) {
                continue;
            }
            SkASSERT(span.fWindValue == 1 || winding == 0);
            SkASSERT(span.fWindSum == SK_MinS32 || span.fWindSum == winding);
        #if DEBUG_MARK_DONE
            const SkPoint& pt = xyAtT(&span);
            SkDebugf("%s segment=%d index=%d t=%1.9g pt=(%1.9g,%1.9g) wind=%d\n",
                    __FUNCTION__, fID, index, span.fT, pt.fX, pt.fY, winding);
        #endif
            span.fWindSum = winding;
        } while (++index < fTs.count() && fTs[index].fT - referenceT < FLT_EPSILON);
    }

    bool multipleSpans(int end, int step) const {
        return step > 0 ? ++end < fTs.count() : end > 0;
    }

    // This has callers for two different situations: one establishes the end
    // of the current span, and one establishes the beginning of the next span
    // (thus the name). When this is looking for the end of the current span,
    // coincidence is found when the beginning Ts contain -step and the end
    // contains step. When it is looking for the beginning of the next, the
    // first Ts found can be ignored and the last Ts should contain -step.
    // OPTIMIZATION: probably should split into two functions
    int nextSpan(int from, int step) const {
        const Span& fromSpan = fTs[from];
        int count = fTs.count();
        int to = from;
        while (step > 0 ? ++to < count : --to >= 0) {
            const Span& span = fTs[to];
            if ((step > 0 ? span.fT - fromSpan.fT : fromSpan.fT - span.fT) < FLT_EPSILON) {
                continue;
            }
            return to;
        }
        return -1;
    }

    const SkPoint* pts() const {
        return fPts;
    }

    void reset() {
        init(NULL, (SkPath::Verb) -1);
        fBounds.set(SK_ScalarMax, SK_ScalarMax, SK_ScalarMax, SK_ScalarMax);
        fTs.reset();
    }

    // OPTIMIZATION: mark as debugging only if used solely by tests
    const Span& span(int tIndex) const {
        return fTs[tIndex];
    }
    
    int spanSign(int startIndex, int endIndex) const {
        return startIndex < endIndex ? -fTs[startIndex].fWindValue :
                fTs[endIndex].fWindValue;
    }

    // OPTIMIZATION: mark as debugging only if used solely by tests
    double t(int tIndex) const {
        return fTs[tIndex].fT;
    }
    
    void updatePts(const SkPoint pts[]) {
        fPts = pts;
    }

    SkPath::Verb verb() const {
        return fVerb;
    }

    // if the only remaining spans are small, ignore them, and mark done
    bool virtuallyDone() {
        int count = fTs.count();
        double previous = 0;
        bool previousDone = fTs[0].fDone;
        for (int index = 1; index < count; ++index) {
            Span& span = fTs[index];
            double t = span.fT;
            if (t - previous < FLT_EPSILON) {
                if (span.fDone && !previousDone) {
                    int prior = --index;
                    int winding = span.fWindSum;
                    do {
                        Span& priorSpan = fTs[prior];
                        priorSpan.fDone = true;
                        priorSpan.fWindSum = winding;
                        fDoneSpans++;
                    } while (--prior >= 0 && t - fTs[prior].fT < FLT_EPSILON);
                }
            } else if (!previousDone) {
                return false;
            }
            previous = t;
            previousDone = span.fDone;
        }
        SkASSERT(done());
        return true;
    }

    int winding(int tIndex) const {
        return fTs[tIndex].fWindSum;
    }
    
    int winding(const Angle* angle) const {
        int start = angle->start();
        int end = angle->end();
        int index = SkMin32(start, end);
        return winding(index);
    }

    int windValue(int tIndex) const {
        return fTs[tIndex].fWindValue;
    }
    
    int windValue(const Angle* angle) const {
        int start = angle->start();
        int end = angle->end();
        int index = SkMin32(start, end);
        return windValue(index);
    }

    SkScalar xAtT(const Span* span) const {
        return xyAtT(span).fX;
    }

    const SkPoint& xyAtT(int index) const {
        return xyAtT(&fTs[index]);
    }

    const SkPoint& xyAtT(const Span* span) const {
        if (!span->fPt) {
            if (span->fT == 0) {
                span->fPt = &fPts[0];
            } else if (span->fT == 1) {
                span->fPt = &fPts[fVerb];
            } else {
                SkPoint* pt = fIntersections.append(); 
                (*SegmentXYAtT[fVerb])(fPts, span->fT, pt);
                span->fPt = pt;
            }
        }
        return *span->fPt;
    }
    
    SkScalar yAtT(int index) const {
        return yAtT(&fTs[index]);
    }

    SkScalar yAtT(const Span* span) const {
        return xyAtT(span).fY;
    }

#if DEBUG_DUMP
    void dump() const {
        const char className[] = "Segment";
        const int tab = 4;
        for (int i = 0; i < fTs.count(); ++i) {
            SkPoint out;
            (*SegmentXYAtT[fVerb])(fPts, t(i), &out);
            SkDebugf("%*s [%d] %s.fTs[%d]=%1.9g (%1.9g,%1.9g) other=%d"
                    " otherT=%1.9g windSum=%d\n",
                    tab + sizeof(className), className, fID,
                    kLVerbStr[fVerb], i, fTs[i].fT, out.fX, out.fY,
                    fTs[i].fOther->fID, fTs[i].fOtherT, fTs[i].fWindSum);
        }
        SkDebugf("%*s [%d] fBounds=(l:%1.9g, t:%1.9g r:%1.9g, b:%1.9g)",
                tab + sizeof(className), className, fID,
                fBounds.fLeft, fBounds.fTop, fBounds.fRight, fBounds.fBottom);
    }
#endif

private:
    const SkPoint* fPts;
    SkPath::Verb fVerb;
    Bounds fBounds;
    SkTDArray<Span> fTs; // two or more (always includes t=0 t=1)
    // OPTIMIZATION:if intersections array is a pointer, the it could only
    // be allocated as needed instead of always initialized -- though maybe
    // the initialization is lightweight enough that it hardly matters
    mutable SkTDArray<SkPoint> fIntersections;
    int fDoneSpans; // used for quick check that segment is finished
#if DEBUG_DUMP
    int fID;
#endif
};

struct Coincidence {
    Segment* fSegments[2];
    double fTs[2][2];
};

class Contour {
public:
    Contour() {
        reset();
#if DEBUG_DUMP
        fID = ++gContourID;
#endif
    }

    bool operator<(const Contour& rh) const {
        return fBounds.fTop == rh.fBounds.fTop
                ? fBounds.fLeft < rh.fBounds.fLeft
                : fBounds.fTop < rh.fBounds.fTop;
    }

    void addCoincident(int index, Contour* other, int otherIndex,
            const Intersections& ts, bool swap) {
        Coincidence& coincidence = *fCoincidences.append();
        coincidence.fSegments[0] = &fSegments[index];
        coincidence.fSegments[1] = &other->fSegments[otherIndex];
        coincidence.fTs[swap][0] = ts.fT[0][0];
        coincidence.fTs[swap][1] = ts.fT[0][1];
        coincidence.fTs[!swap][0] = ts.fT[1][0];
        coincidence.fTs[!swap][1] = ts.fT[1][1];
    }

    void addCross(const Contour* crosser) {
#ifdef DEBUG_CROSS
        for (int index = 0; index < fCrosses.count(); ++index) {
            SkASSERT(fCrosses[index] != crosser);
        }
#endif
        *fCrosses.append() = crosser;
    }

    void addCubic(const SkPoint pts[4]) {
        fSegments.push_back().addCubic(pts);
        fContainsCurves = true;
    }

    int addLine(const SkPoint pts[2]) {
        fSegments.push_back().addLine(pts);
        return fSegments.count();
    }
    
    void addOtherT(int segIndex, int tIndex, double otherT, int otherIndex) {
        fSegments[segIndex].addOtherT(tIndex, otherT, otherIndex);
    }

    int addQuad(const SkPoint pts[3]) {
        fSegments.push_back().addQuad(pts);
        fContainsCurves = true;
        return fSegments.count();
    }

    int addT(int segIndex, double newT, Contour* other, int otherIndex) {
        containsIntercepts();
        return fSegments[segIndex].addT(newT, &other->fSegments[otherIndex]);
    }

    const Bounds& bounds() const {
        return fBounds;
    }
    
    void complete() {
        setBounds();
        fContainsIntercepts = false;
    }

    void containsIntercepts() {
        fContainsIntercepts = true;
    }

    const Segment* crossedSegment(const SkPoint& basePt, SkScalar& bestY, 
            int &tIndex, double& hitT) {
        int segmentCount = fSegments.count();
        const Segment* bestSegment = NULL;
        for (int test = 0; test < segmentCount; ++test) {
            Segment* testSegment = &fSegments[test];
            const SkRect& bounds = testSegment->bounds();
            if (bounds.fTop < bestY) {
                continue;
            }
            if (bounds.fTop > basePt.fY) {
                continue;
            }
            if (bounds.fLeft > basePt.fX) {
                continue;
            }
            if (bounds.fRight < basePt.fX) {
                continue;
            }
            double testHitT;
            int testT = testSegment->crossedSpan(basePt, bestY, testHitT);
            if (testT >= 0) {
                bestSegment = testSegment;
                tIndex = testT;
                hitT = testHitT;
            }
        }
        return bestSegment;
    }
    
    bool crosses(const Contour* crosser) const {
        if (this == crosser) {
            return true;
        }
        for (int index = 0; index < fCrosses.count(); ++index) {
            if (fCrosses[index] == crosser) {
                return true;
            }
        }
        return false;
    }

    void findTooCloseToCall(int winding) {
        int segmentCount = fSegments.count();
        for (int sIndex = 0; sIndex < segmentCount; ++sIndex) {
            fSegments[sIndex].findTooCloseToCall(winding);
        }
    }

    void fixOtherTIndex() {
        int segmentCount = fSegments.count();
        for (int sIndex = 0; sIndex < segmentCount; ++sIndex) {
            fSegments[sIndex].fixOtherTIndex();
        }
    }

    void reset() {
        fSegments.reset();
        fBounds.set(SK_ScalarMax, SK_ScalarMax, SK_ScalarMax, SK_ScalarMax);
        fContainsCurves = fContainsIntercepts = false;
        fWindingSum = SK_MinS32;
    }
    
    void resolveCoincidence(int winding) {
        int count = fCoincidences.count();
        for (int index = 0; index < count; ++index) {
            Coincidence& coincidence = fCoincidences[index];
            Segment* thisOne = coincidence.fSegments[0];
            Segment* other = coincidence.fSegments[1];
            double startT = coincidence.fTs[0][0];
            double endT = coincidence.fTs[0][1];
            if (startT > endT) {
                SkTSwap<double>(startT, endT);
            }
            SkASSERT(endT - startT >= FLT_EPSILON);
            double oStartT = coincidence.fTs[1][0];
            double oEndT = coincidence.fTs[1][1];
            if (oStartT > oEndT) {
                SkTSwap<double>(oStartT, oEndT);
            }
            SkASSERT(oEndT - oStartT >= FLT_EPSILON);
            if (winding > 0 || thisOne->cancels(*other)) {
                thisOne->addTCancel(startT, endT, *other, oStartT, oEndT);
            } else {
                thisOne->addTCoincident(startT, endT, *other, oStartT, oEndT);
            }
        }
    }
    
    const SkTArray<Segment>& segments() {
        return fSegments;
    }
    
    void setWinding(int winding) {
        SkASSERT(fWindingSum < 0);
        fWindingSum = winding;
    }

    // OPTIMIZATION: feel pretty uneasy about this. It seems like once again
    // we need to sort and walk edges in y, but that on the surface opens the
    // same can of worms as before. But then, this is a rough sort based on 
    // segments' top, and not a true sort, so it could be ameniable to regular
    // sorting instead of linear searching. Still feel like I'm missing something
    Segment* topSegment(SkScalar& bestY) {
        int segmentCount = fSegments.count();
        SkASSERT(segmentCount > 0);
        int best = -1;
        Segment* bestSegment = NULL;
        while (++best < segmentCount) {
            Segment* testSegment = &fSegments[best];
        #if 0 // FIXME: remove if not needed
            if (testSegment->virtuallyDone()) {
                continue;
            }
        #else
            if (testSegment->done()) {
                continue;
            }
        #endif
            bestSegment = testSegment;
            break;
        }
        if (!bestSegment) {
            return NULL;
        }
        SkScalar bestTop = bestSegment->activeTop();
        for (int test = best + 1; test < segmentCount; ++test) {
            Segment* testSegment = &fSegments[test];
            if (testSegment->done()) {
                continue;
            }
            if (testSegment->bounds().fTop > bestTop) {
                continue;
            }
            SkScalar testTop = testSegment->activeTop();
            if (bestTop > testTop) {
                bestTop = testTop;
                bestSegment = testSegment;
            }
        }
        bestY = bestTop;
        return bestSegment;
    }

    int updateSegment(int index, const SkPoint* pts) {
        Segment& segment = fSegments[index];
        segment.updatePts(pts);
        return segment.verb() + 1;
    }

    int winding() {
        if (fWindingSum >= 0) {
            return fWindingSum;
        }
        // check peers
        int count = fCrosses.count();
        for (int index = 0; index < count; ++index) {
            const Contour* crosser = fCrosses[index];
            if (0 <= crosser->fWindingSum) {
                fWindingSum = crosser->fWindingSum;
                break;
            }
        }
        return fWindingSum;
    }

#if DEBUG_TEST
    SkTArray<Segment>& debugSegments() {
        return fSegments;
    }
#endif

#if DEBUG_DUMP
    void dump() {
        int i;
        const char className[] = "Contour";
        const int tab = 4;
        SkDebugf("%s %p (contour=%d)\n", className, this, fID);
        for (i = 0; i < fSegments.count(); ++i) {
            SkDebugf("%*s.fSegments[%d]:\n", tab + sizeof(className),
                    className, i);
            fSegments[i].dump();
        }
        SkDebugf("%*s.fBounds=(l:%1.9g, t:%1.9g r:%1.9g, b:%1.9g)\n",
                tab + sizeof(className), className,
                fBounds.fLeft, fBounds.fTop,
                fBounds.fRight, fBounds.fBottom);
        SkDebugf("%*s.fContainsIntercepts=%d\n", tab + sizeof(className),
                className, fContainsIntercepts);
        SkDebugf("%*s.fContainsCurves=%d\n", tab + sizeof(className),
                className, fContainsCurves);
    }
#endif

protected:
    void setBounds() {
        int count = fSegments.count();
        if (count == 0) {
            SkDebugf("%s empty contour\n", __FUNCTION__);
            SkASSERT(0);
            // FIXME: delete empty contour?
            return;
        }
        fBounds = fSegments.front().bounds();
        for (int index = 1; index < count; ++index) {
            fBounds.add(fSegments[index].bounds());
        }
    }

private:
    SkTArray<Segment> fSegments;
    SkTDArray<Coincidence> fCoincidences;
    SkTDArray<const Contour*> fCrosses;
    Bounds fBounds;
    bool fContainsIntercepts;
    bool fContainsCurves;
    int fWindingSum; // initial winding number outside
#if DEBUG_DUMP
    int fID;
#endif
};

class EdgeBuilder {
public:

EdgeBuilder(const SkPath& path, SkTArray<Contour>& contours)
    : fPath(path)
    , fCurrentContour(NULL)
    , fContours(contours)
{
#if DEBUG_DUMP
    gContourID = 0;
    gSegmentID = 0;
#endif
    walk();
}

protected:

void complete() {
    if (fCurrentContour && fCurrentContour->segments().count()) {
        fCurrentContour->complete();
        fCurrentContour = NULL;
    }
}

void walk() {
    // FIXME:remove once we can access path pts directly
    SkPath::RawIter iter(fPath); // FIXME: access path directly when allowed
    SkPoint pts[4];
    SkPath::Verb verb;
    do {
        verb = iter.next(pts);
        *fPathVerbs.append() = verb;
        if (verb == SkPath::kMove_Verb) {
            *fPathPts.append() = pts[0];
        } else if (verb >= SkPath::kLine_Verb && verb <= SkPath::kCubic_Verb) {
            fPathPts.append(verb, &pts[1]);
        }
    } while (verb != SkPath::kDone_Verb);
    // FIXME: end of section to remove once path pts are accessed directly

    SkPath::Verb reducedVerb;
    uint8_t* verbPtr = fPathVerbs.begin();
    const SkPoint* pointsPtr = fPathPts.begin();
    const SkPoint* finalCurveStart = NULL;
    const SkPoint* finalCurveEnd = NULL;
    while ((verb = (SkPath::Verb) *verbPtr++) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kMove_Verb:
                complete();
                if (!fCurrentContour) {
                    fCurrentContour = fContours.push_back_n(1);
                    finalCurveEnd = pointsPtr++;
                    *fExtra.append() = -1; // start new contour
                }
                continue;
            case SkPath::kLine_Verb:
                // skip degenerate points
                if (pointsPtr[-1].fX != pointsPtr[0].fX
                        || pointsPtr[-1].fY != pointsPtr[0].fY) {
                    fCurrentContour->addLine(&pointsPtr[-1]);
                }
                break;
            case SkPath::kQuad_Verb:
                
                reducedVerb = QuadReduceOrder(&pointsPtr[-1], fReducePts);
                if (reducedVerb == 0) {
                    break; // skip degenerate points
                }
                if (reducedVerb == 1) {
                    *fExtra.append() = 
                            fCurrentContour->addLine(fReducePts.end() - 2);
                    break;
                }
                fCurrentContour->addQuad(&pointsPtr[-1]);
                break;
            case SkPath::kCubic_Verb:
                reducedVerb = CubicReduceOrder(&pointsPtr[-1], fReducePts);
                if (reducedVerb == 0) {
                    break; // skip degenerate points
                }
                if (reducedVerb == 1) {
                    *fExtra.append() =
                            fCurrentContour->addLine(fReducePts.end() - 2);
                    break;
                }
                if (reducedVerb == 2) {
                    *fExtra.append() =
                            fCurrentContour->addQuad(fReducePts.end() - 3);
                    break;
                }
                fCurrentContour->addCubic(&pointsPtr[-1]);
                break;
            case SkPath::kClose_Verb:
                SkASSERT(fCurrentContour);
                if (finalCurveStart && finalCurveEnd
                        && *finalCurveStart != *finalCurveEnd) {
                    *fReducePts.append() = *finalCurveStart;
                    *fReducePts.append() = *finalCurveEnd;
                    *fExtra.append() =
                            fCurrentContour->addLine(fReducePts.end() - 2);
                }
                complete();
                continue;
            default:
                SkDEBUGFAIL("bad verb");
                return;
        }
        finalCurveStart = &pointsPtr[verb - 1];
        pointsPtr += verb;
        SkASSERT(fCurrentContour);
    }
    complete();
    if (fCurrentContour && !fCurrentContour->segments().count()) {
        fContours.pop_back();
    }
    // correct pointers in contours since fReducePts may have moved as it grew
    int cIndex = 0;
    int extraCount = fExtra.count();
    SkASSERT(extraCount == 0 || fExtra[0] == -1);
    int eIndex = 0;
    int rIndex = 0;
    while (++eIndex < extraCount) {
        int offset = fExtra[eIndex];
        if (offset < 0) {
            ++cIndex;
            continue;
        }
        fCurrentContour = &fContours[cIndex];
        rIndex += fCurrentContour->updateSegment(offset - 1,
                &fReducePts[rIndex]);
    }
    fExtra.reset(); // we're done with this
}

private:
    const SkPath& fPath;
    SkTDArray<SkPoint> fPathPts; // FIXME: point directly to path pts instead
    SkTDArray<uint8_t> fPathVerbs; // FIXME: remove
    Contour* fCurrentContour;
    SkTArray<Contour>& fContours;
    SkTDArray<SkPoint> fReducePts; // segments created on the fly
    SkTDArray<int> fExtra; // -1 marks new contour, > 0 offsets into contour
};

class Work {
public:
    enum SegmentType {
        kHorizontalLine_Segment = -1,
        kVerticalLine_Segment = 0,
        kLine_Segment = SkPath::kLine_Verb,
        kQuad_Segment = SkPath::kQuad_Verb,
        kCubic_Segment = SkPath::kCubic_Verb,
    };
    
    void addCoincident(Work& other, const Intersections& ts, bool swap) {
        fContour->addCoincident(fIndex, other.fContour, other.fIndex, ts, swap);
    }

    // FIXME: does it make sense to write otherIndex now if we're going to
    // fix it up later?
    void addOtherT(int index, double otherT, int otherIndex) {
        fContour->addOtherT(fIndex, index, otherT, otherIndex);
    }

    // Avoid collapsing t values that are close to the same since
    // we walk ts to describe consecutive intersections. Since a pair of ts can
    // be nearly equal, any problems caused by this should be taken care
    // of later.
    // On the edge or out of range values are negative; add 2 to get end
    int addT(double newT, const Work& other) {
        return fContour->addT(fIndex, newT, other.fContour, other.fIndex);
    }

    bool advance() {
        return ++fIndex < fLast;
    }

    SkScalar bottom() const {
        return bounds().fBottom;
    }

    const Bounds& bounds() const {
        return fContour->segments()[fIndex].bounds();
    }
    
    const SkPoint* cubic() const {
        return fCubic;
    }

    void init(Contour* contour) {
        fContour = contour;
        fIndex = 0;
        fLast = contour->segments().count();
    }
    
    bool isAdjacent(const Work& next) {
        return fContour == next.fContour && fIndex + 1 == next.fIndex;
    }

    bool isFirstLast(const Work& next) {
        return fContour == next.fContour && fIndex == 0
                && next.fIndex == fLast - 1;
    }

    SkScalar left() const {
        return bounds().fLeft;
    }

    void promoteToCubic() {
        fCubic[0] = pts()[0];
        fCubic[2] = pts()[1];
        fCubic[3] = pts()[2];
        fCubic[1].fX = (fCubic[0].fX + fCubic[2].fX * 2) / 3;
        fCubic[1].fY = (fCubic[0].fY + fCubic[2].fY * 2) / 3;
        fCubic[2].fX = (fCubic[3].fX + fCubic[2].fX * 2) / 3;
        fCubic[2].fY = (fCubic[3].fY + fCubic[2].fY * 2) / 3;
    }

    const SkPoint* pts() const {
        return fContour->segments()[fIndex].pts();
    }

    SkScalar right() const {
        return bounds().fRight;
    }

    ptrdiff_t segmentIndex() const {
        return fIndex;
    }

    SegmentType segmentType() const {
        const Segment& segment = fContour->segments()[fIndex];
        SegmentType type = (SegmentType) segment.verb();
        if (type != kLine_Segment) {
            return type;
        }
        if (segment.isHorizontal()) {
            return kHorizontalLine_Segment;
        }
        if (segment.isVertical()) {
            return kVerticalLine_Segment;
        }
        return kLine_Segment;
    }

    bool startAfter(const Work& after) {
        fIndex = after.fIndex;
        return advance();
    }

    SkScalar top() const {
        return bounds().fTop;
    }

    SkPath::Verb verb() const {
        return fContour->segments()[fIndex].verb();
    }

    SkScalar x() const {
        return bounds().fLeft;
    }

    bool xFlipped() const {
        return x() != pts()[0].fX;
    }

    SkScalar y() const {
        return bounds().fTop;
    }

    bool yFlipped() const {
        return y() != pts()[0].fY;
    }

protected:
    Contour* fContour;
    SkPoint fCubic[4];
    int fIndex;
    int fLast;
};

#if DEBUG_ADD_INTERSECTING_TS
static void debugShowLineIntersection(int pts, const Work& wt,
        const Work& wn, const double wtTs[2], const double wnTs[2]) {
    if (!pts) {
        SkDebugf("%s no intersect (%1.9g,%1.9g %1.9g,%1.9g) (%1.9g,%1.9g %1.9g,%1.9g)\n",
                __FUNCTION__, wt.pts()[0].fX, wt.pts()[0].fY,
                wt.pts()[1].fX, wt.pts()[1].fY, wn.pts()[0].fX, wn.pts()[0].fY,
                wn.pts()[1].fX, wn.pts()[1].fY);
        return;
    }
    SkPoint wtOutPt, wnOutPt;
    LineXYAtT(wt.pts(), wtTs[0], &wtOutPt);
    LineXYAtT(wn.pts(), wnTs[0], &wnOutPt);
    SkDebugf("%s wtTs[0]=%g (%g,%g, %g,%g) (%g,%g)",
            __FUNCTION__,
            wtTs[0], wt.pts()[0].fX, wt.pts()[0].fY,
            wt.pts()[1].fX, wt.pts()[1].fY, wtOutPt.fX, wtOutPt.fY);
    if (pts == 2) {
        SkDebugf(" wtTs[1]=%g", wtTs[1]);
    }
    SkDebugf(" wnTs[0]=%g (%g,%g, %g,%g) (%g,%g)\n",
            wnTs[0], wn.pts()[0].fX, wn.pts()[0].fY,
            wn.pts()[1].fX, wn.pts()[1].fY, wnOutPt.fX, wnOutPt.fY);
    if (pts == 2) {
        SkDebugf(" wnTs[1]=%g", wnTs[1]);
    SkDebugf("\n");
    }
#else
static void debugShowLineIntersection(int , const Work& ,
        const Work& , const double [2], const double [2]) {
}
#endif

static bool addIntersectTs(Contour* test, Contour* next) {

    if (test != next) {
        if (test->bounds().fBottom < next->bounds().fTop) {
            return false;
        }
        if (!Bounds::Intersects(test->bounds(), next->bounds())) {
            return true;
        }
    }
    Work wt;
    wt.init(test);
    bool foundCommonContour = test == next;
    do {
        Work wn;
        wn.init(next);
        if (test == next && !wn.startAfter(wt)) {
            continue;
        }
        do {
            if (!Bounds::Intersects(wt.bounds(), wn.bounds())) {
                continue;
            }
            int pts;
            Intersections ts;
            bool swap = false;
            switch (wt.segmentType()) {
                case Work::kHorizontalLine_Segment:
                    swap = true;
                    switch (wn.segmentType()) {
                        case Work::kHorizontalLine_Segment:
                        case Work::kVerticalLine_Segment:
                        case Work::kLine_Segment: {
                            pts = HLineIntersect(wn.pts(), wt.left(),
                                    wt.right(), wt.y(), wt.xFlipped(), ts);
                            debugShowLineIntersection(pts, wt, wn,
                                    ts.fT[1], ts.fT[0]);
                            break;
                        }
                        case Work::kQuad_Segment: {
                            pts = HQuadIntersect(wn.pts(), wt.left(),
                                    wt.right(), wt.y(), wt.xFlipped(), ts);
                            break;
                        }
                        case Work::kCubic_Segment: {
                            pts = HCubicIntersect(wn.pts(), wt.left(),
                                    wt.right(), wt.y(), wt.xFlipped(), ts);
                            break;
                        }
                        default:
                            SkASSERT(0);
                    }
                    break;
                case Work::kVerticalLine_Segment:
                    swap = true;
                    switch (wn.segmentType()) {
                        case Work::kHorizontalLine_Segment:
                        case Work::kVerticalLine_Segment:
                        case Work::kLine_Segment: {
                            pts = VLineIntersect(wn.pts(), wt.top(),
                                    wt.bottom(), wt.x(), wt.yFlipped(), ts);
                            debugShowLineIntersection(pts, wt, wn,
                                    ts.fT[1], ts.fT[0]);
                            break;
                        }
                        case Work::kQuad_Segment: {
                            pts = VQuadIntersect(wn.pts(), wt.top(),
                                    wt.bottom(), wt.x(), wt.yFlipped(), ts);
                            break;
                        }
                        case Work::kCubic_Segment: {
                            pts = VCubicIntersect(wn.pts(), wt.top(),
                                    wt.bottom(), wt.x(), wt.yFlipped(), ts);
                            break;
                        }
                        default:
                            SkASSERT(0);
                    }
                    break;
                case Work::kLine_Segment:
                    switch (wn.segmentType()) {
                        case Work::kHorizontalLine_Segment:
                            pts = HLineIntersect(wt.pts(), wn.left(),
                                    wn.right(), wn.y(), wn.xFlipped(), ts);
                            debugShowLineIntersection(pts, wt, wn,
                                    ts.fT[1], ts.fT[0]);
                            break;
                        case Work::kVerticalLine_Segment:
                            pts = VLineIntersect(wt.pts(), wn.top(),
                                    wn.bottom(), wn.x(), wn.yFlipped(), ts);
                            debugShowLineIntersection(pts, wt, wn,
                                    ts.fT[1], ts.fT[0]);
                            break;
                        case Work::kLine_Segment: {
                            pts = LineIntersect(wt.pts(), wn.pts(), ts);
                            debugShowLineIntersection(pts, wt, wn,
                                    ts.fT[1], ts.fT[0]);
                            break;
                        }
                        case Work::kQuad_Segment: {
                            swap = true;
                            pts = QuadLineIntersect(wn.pts(), wt.pts(), ts);
                            break;
                        }
                        case Work::kCubic_Segment: {
                            swap = true;
                            pts = CubicLineIntersect(wn.pts(), wt.pts(), ts);
                            break;
                        }
                        default:
                            SkASSERT(0);
                    }
                    break;
                case Work::kQuad_Segment:
                    switch (wn.segmentType()) {
                        case Work::kHorizontalLine_Segment:
                            pts = HQuadIntersect(wt.pts(), wn.left(),
                                    wn.right(), wn.y(), wn.xFlipped(), ts);
                            break;
                        case Work::kVerticalLine_Segment:
                            pts = VQuadIntersect(wt.pts(), wn.top(),
                                    wn.bottom(), wn.x(), wn.yFlipped(), ts);
                            break;
                        case Work::kLine_Segment: {
                            pts = QuadLineIntersect(wt.pts(), wn.pts(), ts);
                            break;
                        }
                        case Work::kQuad_Segment: {
                            pts = QuadIntersect(wt.pts(), wn.pts(), ts);
                            break;
                        }
                        case Work::kCubic_Segment: {
                            wt.promoteToCubic();
                            pts = CubicIntersect(wt.cubic(), wn.pts(), ts);
                            break;
                        }
                        default:
                            SkASSERT(0);
                    }
                    break;
                case Work::kCubic_Segment:
                    switch (wn.segmentType()) {
                        case Work::kHorizontalLine_Segment:
                            pts = HCubicIntersect(wt.pts(), wn.left(),
                                    wn.right(), wn.y(), wn.xFlipped(), ts);
                            break;
                        case Work::kVerticalLine_Segment:
                            pts = VCubicIntersect(wt.pts(), wn.top(),
                                    wn.bottom(), wn.x(), wn.yFlipped(), ts);
                            break;
                        case Work::kLine_Segment: {
                            pts = CubicLineIntersect(wt.pts(), wn.pts(), ts);
                            break;
                        }
                        case Work::kQuad_Segment: {
                            wn.promoteToCubic();
                            pts = CubicIntersect(wt.pts(), wn.cubic(), ts);
                            break;
                        }
                        case Work::kCubic_Segment: {
                            pts = CubicIntersect(wt.pts(), wn.pts(), ts);
                            break;
                        }
                        default:
                            SkASSERT(0);
                    }
                    break;
                default:
                    SkASSERT(0);
            }
            if (!foundCommonContour && pts > 0) {
                test->addCross(next);
                next->addCross(test);
                foundCommonContour = true;
            }
            // in addition to recording T values, record matching segment
            if (pts == 2 && wn.segmentType() <= Work::kLine_Segment
                    && wt.segmentType() <= Work::kLine_Segment) {
                if (wt.isAdjacent(wn)) {
                    int testEndTAt = wt.addT(1, wn);
                    int nextEndTAt = wn.addT(0, wt);
                    wt.addOtherT(testEndTAt, 0, nextEndTAt);
                    wn.addOtherT(nextEndTAt, 1, testEndTAt);
                }
                if (wt.isFirstLast(wn)) {
                    int testStartTAt = wt.addT(0, wn);
                    int nextStartTAt = wn.addT(1, wt);
                    wt.addOtherT(testStartTAt, 1, nextStartTAt);
                    wn.addOtherT(nextStartTAt, 0, testStartTAt);
                }
                wt.addCoincident(wn, ts, swap);
                continue;
            }
            for (int pt = 0; pt < pts; ++pt) {
                SkASSERT(ts.fT[0][pt] >= 0 && ts.fT[0][pt] <= 1);
                SkASSERT(ts.fT[1][pt] >= 0 && ts.fT[1][pt] <= 1);
                int testTAt = wt.addT(ts.fT[swap][pt], wn);
                int nextTAt = wn.addT(ts.fT[!swap][pt], wt);
                wt.addOtherT(testTAt, ts.fT[!swap][pt], nextTAt);
                wn.addOtherT(nextTAt, ts.fT[swap][pt], testTAt);
            }
        } while (wn.advance());
    } while (wt.advance());
    return true;
}

// resolve any coincident pairs found while intersecting, and
// see if coincidence is formed by clipping non-concident segments
static void coincidenceCheck(SkTDArray<Contour*>& contourList, int winding) {
    int contourCount = contourList.count();
    for (int cIndex = 0; cIndex < contourCount; ++cIndex) {
        Contour* contour = contourList[cIndex];
        contour->resolveCoincidence(winding);
    }
    for (int cIndex = 0; cIndex < contourCount; ++cIndex) {
        Contour* contour = contourList[cIndex];
        contour->findTooCloseToCall(winding);
    }
}

// project a ray from the top of the contour up and see if it hits anything
// note: when we compute line intersections, we keep track of whether
// two contours touch, so we need only look at contours not touching this one.
// OPTIMIZATION: sort contourList vertically to avoid linear walk
static int innerContourCheck(SkTDArray<Contour*>& contourList,
        Contour* baseContour, const SkPoint& basePt) {
    int contourCount = contourList.count();
    int winding = 0;
    SkScalar bestY = SK_ScalarMin;
    for (int cTest = 0; cTest < contourCount; ++cTest) {
        Contour* contour = contourList[cTest];
        if (basePt.fY < contour->bounds().fTop) {
            continue;
        }
        if (bestY > contour->bounds().fBottom) {
            continue;
        }
        if (baseContour->crosses(contour)) {
           continue;
        }
        int tIndex;
        double tHit;
        const Segment* test = contour->crossedSegment(basePt, bestY, tIndex,
            tHit);
        if (!test) {
            continue;
        }
        // If the ray hit the end of a span, we need to construct the wheel of
        // angles to find the span closest to the ray -- even if there are just
        // two spokes on the wheel.
        if (tHit == test->t(tIndex)) {
            SkTDArray<Angle> angles;
            int end = test->nextSpan(tIndex, 1);
            if (end < 0) {
                end = test->nextSpan(tIndex, -1);
            }
            test->addTwoAngles(tIndex, end, angles);
     //       test->buildAnglesInner(tIndex, angles);
            test->buildAngles(tIndex, angles);
            SkTDArray<Angle*> sorted;
            sortAngles(angles, sorted);
            const Angle* angle = sorted[0];
            test = angle->segment();
            SkScalar testDx = (*SegmentDXAtT[test->verb()])(test->pts(), tHit);
            if (testDx == 0) {
                angle = *(sorted.end() - 1);
                test = angle->segment();
                SkASSERT((*SegmentDXAtT[test->verb()])(test->pts(), tHit) != 0);
            }
            tIndex = angle->start(); // lesser Y
            winding = test->winding(SkMin32(tIndex, angle->end()));
    #if DEBUG_WINDING
           SkDebugf("%s 1 winding=%d\n", __FUNCTION__, winding);
    #endif
        } else {
            winding = test->winding(tIndex);
    #if DEBUG_WINDING
            SkDebugf("%s 2 winding=%d\n", __FUNCTION__, winding);
    #endif
        }
        // see if a + change in T results in a +/- change in X (compute x'(T))
        SkScalar dx = (*SegmentDXAtT[test->verb()])(test->pts(), tHit);
    #if DEBUG_WINDING
        SkDebugf("%s dx=%1.9g\n", __FUNCTION__, dx);
    #endif
        SkASSERT(dx != 0);
        if (winding * dx > 0) { // if same signs, result is negative
            winding += dx > 0 ? -1 : 1;
    #if DEBUG_WINDING
            SkDebugf("%s 3 winding=%d\n", __FUNCTION__, winding);
    #endif
        }
    }
    baseContour->setWinding(winding);
    return winding;
}
    
// OPTIMIZATION: not crazy about linear search here to find top active y.
// seems like we should break down and do the sort, or maybe sort each
// contours' segments? 
// Once the segment array is built, there's no reason I can think of not to
// sort it in Y. hmmm
// FIXME: return the contour found to pass to inner contour check
static Segment* findTopContour(SkTDArray<Contour*>& contourList,
        Contour*& topContour) {
    int contourCount = contourList.count();
    int cIndex = 0;
    Segment* topStart;
    SkScalar bestY = SK_ScalarMax;
    Contour* contour;
    do {
        contour = contourList[cIndex];
        topStart = contour->topSegment(bestY);
    } while (!topStart && ++cIndex < contourCount);
    if (!topStart) {
        return NULL;
    }
    topContour = contour;
    while (++cIndex < contourCount) {
        contour = contourList[cIndex];
        if (bestY < contour->bounds().fTop) {
            continue;
        }
        SkScalar testY = SK_ScalarMax;
        Segment* test = contour->topSegment(testY);
        if (!test || bestY <= testY) {
            continue;
        }
        topContour = contour;
        topStart = test;
        bestY = testY;
    }
    return topStart;
}

// Each segment may have an inside or an outside. Segments contained within
// winding may have insides on either side, and form a contour that should be
// ignored. Segments that are coincident with opposing direction segments may
// have outsides on either side, and should also disappear.
// 'Normal' segments will have one inside and one outside. Subsequent connections
// when winding should follow the intersection direction. If more than one edge
// is an option, choose first edge that continues the inside.
    // since we start with leftmost top edge, we'll traverse through a
    // smaller angle counterclockwise to get to the next edge.  
static void bridge(SkTDArray<Contour*>& contourList, SkPath& simple) {
    // after findTopContour has already been called once, check if
    // result of subsequent findTopContour has no winding set
    bool firstContour = true;
    do {
        Contour* topContour;
        Segment* topStart = findTopContour(contourList, topContour);
        if (!topStart) {
            break;
        }
        // Start at the top. Above the top is outside, below is inside.
        // follow edges to intersection by changing the index by direction.
        int index, endIndex;
        Segment* current = topStart->findTop(index, endIndex);
        int winding = 0;
        if (!firstContour) {
            int contourWinding = topContour->winding();
    #if DEBUG_WINDING
            SkDebugf("%s 1 winding=%d\n", __FUNCTION__, winding);
    #endif
            if (contourWinding == SK_MinS32) {
                const SkPoint& topPoint = current->xyAtT(endIndex);
                winding = innerContourCheck(contourList, topContour, topPoint);
    #if DEBUG_WINDING
                SkDebugf("%s 2 winding=%d\n", __FUNCTION__, winding);
    #endif
            }
        }
        const SkPoint* firstPt = NULL;
        SkPoint lastPt;
        bool firstTime = true;
        int spanWinding = current->spanSign(index, endIndex);
        if (firstContour) {
            topContour->setWinding(spanWinding);
            firstContour = false;
        }
        bool active = winding * spanWinding <= 0;
        do {
            SkASSERT(!current->done());
            int nextStart, nextEnd;
            Segment* next = current->findNext(winding + spanWinding, index,
                    endIndex, nextStart, nextEnd, firstTime);
            if (!next) {
                break;
            }
            if (!firstPt) {
                firstPt = &current->addMoveTo(index, simple, active);
            }
            lastPt = current->addCurveTo(index, endIndex, simple, active);
            current = next;
            index = nextStart;
            endIndex = nextEnd;
            spanWinding = SkSign32(spanWinding) * next->windValue(
                    SkMin32(nextStart, nextEnd));
    #if DEBUG_WINDING
            SkDebugf("%s spanWinding=%d\n", __FUNCTION__, spanWinding);
    #endif
            firstTime = false;
        } while (*firstPt != lastPt);
        if (firstPt) {
    #if DEBUG_PATH_CONSTRUCTION
            SkDebugf("%s close\n", __FUNCTION__);
    #endif
            simple.close();
        }
    } while (true);
}

static void fixOtherTIndex(SkTDArray<Contour*>& contourList) {
    int contourCount = contourList.count();
    for (int cTest = 0; cTest < contourCount; ++cTest) {
        Contour* contour = contourList[cTest];
        contour->fixOtherTIndex();
    }
}

static void makeContourList(SkTArray<Contour>& contours,
        SkTDArray<Contour*>& list) {
    int count = contours.count();
    if (count == 0) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        *list.append() = &contours[index];
    }
    QSort<Contour>(list.begin(), list.end() - 1);
}

void simplifyx(const SkPath& path, SkPath& simple) {
    // returns 1 for evenodd, -1 for winding, regardless of inverse-ness
    int winding = (path.getFillType() & 1) ? 1 : -1;
    simple.reset();
    simple.setFillType(SkPath::kEvenOdd_FillType);

    // turn path into list of segments
    SkTArray<Contour> contours;
    // FIXME: add self-intersecting cubics' T values to segment
    EdgeBuilder builder(path, contours);
    SkTDArray<Contour*> contourList;
    makeContourList(contours, contourList);
    Contour** currentPtr = contourList.begin();
    if (!currentPtr) {
        return;
    }
    Contour** listEnd = contourList.end();
    // find all intersections between segments
    do {
        Contour** nextPtr = currentPtr;
        Contour* current = *currentPtr++;
        Contour* next;
        do {
            next = *nextPtr++;
        } while (addIntersectTs(current, next) && nextPtr != listEnd);
    } while (currentPtr != listEnd);
    // eat through coincident edges
    coincidenceCheck(contourList, winding);
    fixOtherTIndex(contourList);
    // construct closed contours
    bridge(contourList, simple);
}

