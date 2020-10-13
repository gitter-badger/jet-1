#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define for_to(i, n) for (int(i) = 0; (i) < (n); (i)++)

#define POW2(n) (1 << n)
#define MIN(a, b) (a) < (b) ? (a) : (b)

#define DIMS 3
const int POINTS_PER_LEAF = MIN(POW2(DIMS), 8);

typedef struct KDTreeNode {
    double threshold[DIMS]; // [D] // leaves dont hav thresholds!
    int npoints, isleaf; // GET THIS OUT HOWEVER
    union {
        struct KDTreeNode* child[POW2(DIMS)]; // [2^D]
        struct KDTreePoint* points[POINTS_PER_LEAF];
    };
} KDTreeNode;

typedef struct KDTreePoint {
    double x[DIMS]; // [D]
} KDTreePoint;

double squared(double x) { return x * x; }
double distanceFunction(KDTreePoint* p, KDTreePoint* p2)
{
    double sum = 0;
    for_to(i, DIMS) sum += squared(p->x[i] - p2->x[i]);
    return sum;
}

void addPoint(KDTreeNode* node, KDTreePoint* point)
{
    int dird[DIMS], direction = 0;
    for_to(i, DIMS) dird[i] = (point->x[i] >= node->threshold[i]) << i;
    // dird[i] tells you whether the ith-coordinate of the point is below or
    // above (or if you prefer, to the left or right of) the ith-coordinate of
    // the node's threshold. You need to check all dimensions and find out how
    // the comparison goes (individually). In a simple binary tree this is like
    // deciding whether the new value should go to the left or to the right
    // child, but to avoid branching here it goes to child[0] or child[1], and
    // the 0 or 1 comes from the comparison result. node->child is actually
    // effectively node->child[][][] ... i.e. as many []s as the dimensionality.
    // However writing it explicitly as a multi-dim array makes things difficult
    // for a truly generic version, so I'm going to write it as a normal array
    // and calculate the overall (flat) index. this N-dimensional access of the
    // form arr[l1][l2][l3] is effectively l1*4 + l2*2 + l3 ... and so on for
    // higher dimensions, where l's are either 0 or 1. so it can be reduced to
    // bitwise ops: the *s can be done with << and adding them together is a |.
    for_to(i, DIMS) direction |= dird[i]; // JUST PUT THIS IN THE ABOVE LOOP
    // direction is now the overall index of the "correct" child to follow.

    // now 3 things can happen: (1) the child doesn't exist, (2) it does
    // and is a KDTreeNode, (3) it does and is a leaf (it has KDTreePoint[]).
    // Descend until just before you hit a leaf or NULL.
    KDTreeNode* parent = NULL;
    while (node->child[direction] && !node->child[direction]->isleaf) {
        parent = node;
        node = node->child[direction];
        // direction = (point->x >= node->threshold);
        direction = 0;
        for_to(i, DIMS) dird[i] = (point->x[i] >= node->threshold[i]) << i;
        for_to(i, DIMS) direction |= dird[i];
    }

    // now the child is either NULL or a leaf (i.e. has points)
    if (!node->child[direction]) {
        node->child[direction] = calloc(1, sizeof(KDTreeNode));
        node->child[direction]->isleaf = 1;
    }

    // now the child must be a leaf
    KDTreeNode* child = node->child[direction];

    if (child->npoints == POINTS_PER_LEAF) {
        // the array of points in this leaf is full. the leaf should now be
        // converted into a node and the POINTS_PER_LEAF points of this ex-leaf
        // should be re-added into this node so so they can go one level deeper
        // in the tree at the right spots.
        KDTreePoint* points[POINTS_PER_LEAF] = {};
        memcpy(points, child->points, sizeof(KDTreePoint*) * POINTS_PER_LEAF);
        child->isleaf = 0; // no longer a leaf.
        child->npoints = 0; // don't need it here, its not a leaf
        bzero(child->points, sizeof(KDTreePoint*) * POINTS_PER_LEAF);
        // wipe the points, the space will be used
        // for child node ptrs now

        // and you know that there are POINTS_PER_LEAF points anyway.

        // you have to add 2 grandchildren here (actually 2^NDIMS). Don't leave
        // them at NULL. Set them as leaves.
        // TODO: figure out how to only create child->child[direction] and not
        // all 2^DIMS children here
        for (int i = direction; i == direction /*POW2(DIMS)*/; i++) {
            // for (int i = 0; i < POW2(DIMS); i++) {
            child->child[i] = calloc(1, sizeof(KDTreeNode));
            child->child[i]->isleaf = 1;
        }

        for_to(i, DIMS)
        { // you need the midpoints of the new grandkids.
            // they depend on the midpoint of the child, and the midpoint of the
            // parent (i.e. two levels up the ancestry). For these you first
            // need 3 (!) "limits", the lo/mid/hi limits.
            double th_mid = node->threshold[i];
            double th_lo, th_hi;
            if (node->threshold[i] >= parent->threshold[i]) {
                // child is increasing, i.e. going to the right
                // Lower bound is the parent's threshold.
                th_lo = parent->threshold[i];
                th_hi = 2 * node->threshold[i] - parent->threshold[i];
            } else {
                // child is decreasing, i.e. going to the left.
                // Higher bound is the parent's threshold.
                th_lo = 2 * node->threshold[i] - parent->threshold[i];
                th_hi = parent->threshold[i];
            }

            if (!dird[i])
                child->threshold[i] = (th_mid + th_lo) / 2;
            else
                child->threshold[i] = (th_hi + th_mid) / 2;
        }

        // Now you see if the grandkid is increasing or decreasing compared to
        // the child (the grandkid's parent). Again you have to do this for all
        // grandchildren, regardless of whether they have any points (yet).
        //        for (int gdirection = 0; gdirection < 2; gdirection++)
        //            // for (int i=0;i<2^NDIMS;i++) for (int j=0;j<NDIMS;j++)
        //            gdird[j] =
        //            // i & 1<<j; gdirection |= ...
        //            if (gdirection)
        //                child->child[gdirection]->threshold = (th_hi - th_mid)
        //                / 2;
        //            else
        //                child->child[gdirection]->threshold = (th_mid - th_lo)
        //                / 2;

        // now that the thresholds are set, add the cached points
        for (int ip = 0; ip < POINTS_PER_LEAF; ip++)
            addPoint(child, points[ip]);

        child = child->child[direction];

    } // TODO: what to do if npoints was somehow set to > POINTS_PER_LEAF?

    // NOW finally add the point.
    // you have a static array of POINTS_PER_LEAF items, but deleting leaves
    // holes. So add will have to loop and find an open hole to plug the new
    // point in. its probably better than rearranging the array on delete,
    // because when POINTS_PER_LEAF is small the array is in cache anyway, but
    // writing to it will access DRAM.
    for (int i = 0; i < POINTS_PER_LEAF; i++)
        if (!child->points[i]) {
            child->points[i] = point;
            child->npoints++;
            break;
        }
}

void removePoint(KDTreeNode* node, KDTreePoint* point) { }

// always init  with this func with a known number of levels and a bounding box
// this way you have a node hierarchy ready and your add calls will not thrash
// adding subnodes and moving points down over and over.
// KDTreeNode* init(int levels, KDTreePoint* boundBox[2]) { }

// BTW for jet do you want to allow function xyz(arr[2]) etc. and then check
// at callsites if passed array has exactly 2 (or 2 or more) provably?

KDTreePoint* getNearestPoint(KDTreeNode* node, KDTreePoint* point)
{
    // TODO: PROBLEM!!! HERE you descend into the finest octant/quadrnt and
    // check only those points. The actual nearest point may be in an adjacent
    // quadrant! So you should go up 1 level than the finest, or 2?

    // here as opposed to add, you descend all the way down to the leaf.
    while (node && !node->isleaf) {
        int direction = 0, dird[DIMS];
        for_to(i, DIMS) dird[i] = (point->x[i] >= node->threshold[i]) << i;
        for_to(i, DIMS) direction |= dird[i];
        node = node->child[direction];
    }
    // actually you shouldn't expect to find a NULL leaf here.
    int minIndex = 0;
    double mindist = 1e300;
    if (!node) return NULL;
    for (int i = 0; i < node->npoints; i++) {
        double dist = distanceFunction(node->points[i], point);
        if (dist < mindist) {
            minIndex = i;
            mindist = dist;
        }
    }
    // a point is always returned. you asked for the near*est* point, and there
    // is always one, regardless of how near or far it is exactly.
    // THATS NOT TRUE!!!!!
    // MAYBE: pass mindist back to the caller to save a repeated
    // distanceFunction call.
    return node->points[minIndex];
}
// static KDTreePoint points[] = { { 1 }, { 3 }, { -5 } };

#define countof(x) (sizeof(x) / sizeof(x[0]))
static const char* const spc = "                                            ";
const int levStep = 2;
void printPoint(KDTreePoint* p, int lev)
{
    printf("%.*s(", lev, spc);
    for_to(i, DIMS) { printf("%s%g", i ? ", " : "", p->x[i]); }
    puts(")");
}

void printNode(KDTreeNode* node, int lev)
{
    if (node->isleaf) {
        printf("[\n"); //,node->npoints );
        for_to(j, node->npoints) { printPoint(node->points[j], lev + levStep); }
        printf("%.*s]\n", lev, spc);
    } else {
        printf("%.*s<", 0 * lev, spc);
        for_to(i, DIMS) { printf("%s%g", i ? ", " : "", node->threshold[i]); }
        printf("> {\n");
        for_to(i, POW2(DIMS))
        {
            if (node->child[i]) {
                printf("%.*s%d: ", lev + levStep, spc, i);
                printNode(node->child[i], lev + levStep);
            } else {
                // printf("%.*schild[%d] NULL\n", lev, spc,i);
            }
        }
        printf("%.*s}\n", lev, spc);
    }
}

void printDotRec(FILE* f, KDTreeNode* node)
{
    for_to(i, POW2(DIMS))
    {
        int dird[DIMS];
        for_to(j, DIMS) dird[j] = i & (1 << j);

        if (node->child[i]) {
            if (node->child[i]->isleaf) {
                fprintf(f,
                    "\"Node\\n<%g, %g, %g>\" -> \"Points[%d]\\n____________\\n",
                    node->threshold[0], node->threshold[1], node->threshold[2],
                    node->child[i]->npoints);
                for_to(j, node->child[i]->npoints)
                {
                    KDTreePoint* point = node->child[i]->points[j];
                    fprintf(f, "(%g, %g, %g)\\n", point->x[0], point->x[1],
                        point->x[2]);
                }
                fprintf(f, "\" [label=\"[%d]\\n", i);
                for_to(j, DIMS) fprintf(f, "%c", dird[j] ? '>' : '<');
                fprintf(f, "\"]\n");
            } else {
                fprintf(f,
                    "\"Node\\n<%g, %g, %g>\" -> \"Node\\n<%g, %g, %g>\" "
                    "[label=\"[%d]\\n",
                    node->threshold[0], node->threshold[1], node->threshold[2],
                    node->child[i]->threshold[0], node->child[i]->threshold[1],
                    node->child[i]->threshold[2], i);
                for_to(j, DIMS) fprintf(f, "%c", dird[j] ? '>' : '<');
                fprintf(f, "\"]\n");

                printDotRec(f, node->child[i]);
            }
        }
    }
}

void printDot(KDTreeNode* node)
{
    FILE* f = fopen("kdt.dot", "w");
    fputs(
        "digraph {\nnode [fontname=\"Miriam Libre\"]; edge [fontname=\"Miriam "
        "Libre\"];\n",
        f);
    printDotRec(f, node);
    fputs("}\n", f);
    fclose(f);
}

static KDTreePoint g_Points[] = { { -5 }, { -4 }, { -3 }, { -2 }, { 6 },
    { -5.35 }, { -4.35 }, { -3.35 }, { -2.4545 }, { 1.45 }, { -5.3578 },
    { -4.6789 }, { -3.8679 }, { -2.5557 }, { -1.6767 }, { -5.10354225 },
    { -4.035 }, { -3.3335 }, { -2.45 }, { -1.04455 } };

static KDTreePoint gPoints[] = {
    { -0, -2, 3 }, //
    { -0, 1, 4 }, //
    { -0, 2, -4 }, //
    { -0, 5, -2 }, //
    { -1, -2, -3 }, //
    { -1, -3, 4 }, //
    { -1, -5, -2 }, //
    { -1, 0, 5 }, //
    { -1, 1, 1 }, //
    { -1, 3, 0 }, //
    { -1, 3, 2 }, //
    { -1, 3, 4 }, //
    { -1, 4, -2 }, //
    { -1, 4, 3 }, //
    { -1, 4, 4 }, //
    { -1, 5, 4 }, //
    { -2, -1, -3 }, //
    { -2, -3, -4 }, //
    { -2, -3, 1 }, //
    { -2, 1, 0 }, //
    { -2, 2, -3 }, //
    { -2, 3, -2 }, //
    { -3, -1, -4 }, //
    { -3, -1, 4 }, //
    { -3, -2, 1 }, //
    { -3, -3, 3 }, //
    { -3, -4, 3 }, //
    { -3, 2, -2 }, //
    { -3, 2, -4 }, //
    { -3, 3, 0 }, //
    { -3, 4, -4 }, //
    { -3, 5, -5 }, //
    { -3, 5, 4 }, //
    { -4, -1, 1 }, //
    { -4, -3, -0 }, //
    { -4, -3, 2 }, //
    { -4, -3, 5 }, //
    { -4, -4, -0 }, //
    { -4, -5, 4 }, //
    { -4, 0, -4 }, //
    { -4, 1, -5 }, //
    { -4, 4, 0 }, //
    { -4, 5, -3 }, //
    { -5, -0, -4 }, //
    { 0, -4, -2 }, //
    { 0, 0, 0 }, //
    { 0, 2, -4 }, //
    { 0, 3, -2 }, //
    { 0, 3, 1 }, //
    { 0, 5, -2 }, //
    { 1, -0, 1 }, //
    { 1, -1, 2 }, //
    { 1, -2, 3 }, //
    { 1, -3, -3 }, //
    { 1, -3, 1 }, //
    { 1, -3, 2 }, //
    { 1, -5, -1 }, //
    { 1, 2, -2 }, //
    { 1, 3, -3 }, //
    { 1, 3, -4 }, //
    { 2, -0, -0 }, //
    { 2, -0, -3 }, //
    { 2, -1, 2 }, //
    { 2, -1, 3 }, //
    { 2, -2, 2 }, //
    { 2, -3, 4 }, //
    { 2, -4, 3 }, //
    { 2, 0, -5 }, //
    { 2, 1, -5 }, //
    { 2, 2, -2 }, //
    { 2, 2, 1 }, //
    { 2, 2, 5 }, //
    { 2, 3, 4 }, //
    { 2, 4, 5 }, //
    { 3, -1, 3 }, //
    { 3, -2, -2 }, //
    { 3, -2, -3 }, //
    { 3, 0, 3 }, //
    { 3, 1, 3 }, //
    { 3, 2, -4 }, //
    { 3, 4, -3 }, //
    { 3, 4, -4 }, //
    { 3, 5, 4 }, //
    { 4, -0, -3 }, //
    { 4, -1, 2 }, //
    { 4, -1, 4 }, //
    { 4, -2, -4 }, //
    { 4, -2, 0 }, //
    { 4, 1, -1 }, //
    { 4, 1, 2 }, //
    { 4, 2, -4 }, //
    { 4, 3, 5 }, //
    { 4, 4, 4 }, //
    { 5, -4, 0 }, //
    { 5, -4, 4 }, //
    { 5, 1, 2 }, //
    { 5, 1, 4 }, //
    { 5, 4, -4 } //
};

KDTreePoint* linsearch(KDTreePoint* points, int npoints, KDTreePoint* point)
{
    int minIndex = 0;
    double mindist = 1e300;
    for_to(i, npoints)
    {
        double dist = distanceFunction(points + i, point);
        if (dist < mindist) {
            minIndex = i;
            mindist = dist;
        }
    }
    return points + minIndex;
}

int main()
{
    KDTreeNode root[] = { { .threshold = { 0 } } };
    KDTreeNode rc1[] = { { .threshold = { -3, -3, -3 } } };
    KDTreeNode rc2[] = { { .threshold = { 3, -3, -3 } } };
    KDTreeNode rc3[] = { { .threshold = { -3, 3, -3 } } };
    KDTreeNode rc4[] = { { .threshold = {} } };
    KDTreeNode rc5[] = { { .threshold = { -3, -3, 3 } } };
    KDTreeNode rc6[] = { { .threshold = { 3, -3, 3 } } };
    KDTreeNode rc7[] = { { .threshold = { -3, 3, 3 } } };
    KDTreeNode rc8[] = { { .threshold = { 3, 3, 3 } } };

    root->child[0] = rc1;
    root->child[1] = rc2;
    root->child[2] = rc3;
    root->child[3] = rc4;
    root->child[4] = rc5;
    root->child[5] = rc6;
    root->child[6] = rc7;
    root->child[7] = rc8;

    for (int i = 0; i < countof(gPoints); i++) { addPoint(root, gPoints + i); }
    printNode(root, 0);
    printDot(root);
    KDTreePoint ptest[] = { { -3.14159 } };
    KDTreePoint* nea = getNearestPoint(root, ptest);
    if (nea)
        printPoint(nea, 0);
    else {
        printf("no match for: ");
        printPoint(ptest, 0);
        printf("expected: ");
        printPoint(linsearch(gPoints, countof(gPoints), ptest), 0);
    }
}
