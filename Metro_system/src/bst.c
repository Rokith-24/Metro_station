
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "bst.h"

/* ---------- AVL helper functions ---------- */

static int getHeight(const StationNode *node)
{
    return (node == NULL) ? 0 : node->height;
}

static int maxInt(int a, int b)
{
    return (a > b) ? a : b;
}

static void updateHeight(StationNode *node)
{
    if (node != NULL) {
        node->height = 1 + maxInt(
            getHeight(node->left),
            getHeight(node->right)
        );
    }
}

static int getBalanceFactor(const StationNode *node)
{
    if (node == NULL) {
        return 0;
    }

    return getHeight(node->left) - getHeight(node->right);
}

/* ---------- AVL rotations ---------- */

/*
 * Right rotation:
 *
 *          y                 x
 *         / \               / \
 *        x   C     ->      A   y
 *       / \                   / \
 *      A   B                 B   C
 */
static StationNode *rotateRight(StationNode *y)
{
    StationNode *x;
    StationNode *subtree;

    if (y == NULL || y->left == NULL) {
        return y;
    }

    x = y->left;
    subtree = x->right;

    x->right = y;
    y->left = subtree;

    updateHeight(y);
    updateHeight(x);

    return x;
}

/*
 * Left rotation:
 *
 *       x                     y
 *      / \                   / \
 *     A   y       ->        x   C
 *        / \               / \
 *       B   C             A   B
 */
static StationNode *rotateLeft(StationNode *x)
{
    StationNode *y;
    StationNode *subtree;

    if (x == NULL || x->right == NULL) {
        return x;
    }

    y = x->right;
    subtree = y->left;

    y->left = x;
    x->right = subtree;

    updateHeight(x);
    updateHeight(y);

    return y;
}

/* ---------- AVL rebalancing ---------- */

static StationNode *rebalance(StationNode *node)
{
    int balance;

    if (node == NULL) {
        return NULL;
    }

    updateHeight(node);
    balance = getBalanceFactor(node);

    /* Left-heavy subtree: LL or LR case */
    if (balance > 1) {
        if (getBalanceFactor(node->left) < 0) {
            /* LR: rotate left on the left child */
            node->left = rotateLeft(node->left);
        }

        /* LL, or the second rotation for LR */
        return rotateRight(node);
    }

    /* Right-heavy subtree: RR or RL case */
    if (balance < -1) {
        if (getBalanceFactor(node->right) > 0) {
            /* RL: rotate right on the right child */
            node->right = rotateRight(node->right);
        }

        /* RR, or the second rotation for RL */
        return rotateLeft(node);
    }

    return node;
}

/* ---------- Node creation and validation ---------- */

StationNode *createNode(Station station)
{
    StationNode *node = malloc(sizeof(*node));

    if (node == NULL) {
        return NULL;
    }

    node->station = station;
    node->height = 1;
    node->left = NULL;
    node->right = NULL;

    return node;
}

static int isValidStation(const Station *station)
{
    if (station == NULL) {
        return 0;
    }

    if (station->id <= 0 ||
        station->name[0] == '\0' ||
        memchr(station->name, '\0',
               sizeof(station->name)) == NULL ||
        station->distance < 0.0f ||
        !isfinite(station->distance) ||
        station->fare < 0.0f ||
        !isfinite(station->fare) ||
        station->platform <= 0) {
        return 0;
    }

    return 1;
}

/* ---------- Search ---------- */

StationNode *searchStation(StationNode *root, int id)
{
    while (root != NULL) {
        if (id == root->station.id) {
            return root;
        }

        if (id < root->station.id) {
            root = root->left;
        } else {
            root = root->right;
        }
    }

    return NULL;
}

/* ---------- AVL insertion ---------- */

static StationNode *insertNode(
    StationNode *root,
    Station station,
    AppStatus *status)
{
    if (root == NULL) {
        StationNode *node = createNode(station);

        if (node == NULL) {
            *status = APP_ERROR;
        }

        return node;
    }

    if (station.id == root->station.id) {
        *status = APP_DUPLICATE;
        return root;
    }

    if (station.id < root->station.id) {
        StationNode *newLeft = insertNode(
            root->left, station, status
        );

        /*
         * If allocation failed, preserve the existing tree.
         */
        if (*status == APP_ERROR) {
            return root;
        }

        root->left = newLeft;
    } else {
        StationNode *newRight = insertNode(
            root->right, station, status
        );

        if (*status == APP_ERROR) {
            return root;
        }

        root->right = newRight;
    }

    if (*status != APP_SUCCESS) {
        return root;
    }

    return rebalance(root);
}

AppStatus insertStation(StationNode **root, Station station)
{
    AppStatus status = APP_SUCCESS;

    if (root == NULL || !isValidStation(&station)) {
        return APP_INVALID_INPUT;
    }

    *root = insertNode(*root, station, &status);

    return status;
}

/* ---------- AVL update ---------- */

AppStatus updateStation(
    StationNode *root,
    int id,
    Station updatedStation)
{
    StationNode *node;

    if (id <= 0 || !isValidStation(&updatedStation)) {
        return APP_INVALID_INPUT;
    }

    if (updatedStation.id != id) {
        return APP_INVALID_INPUT;
    }

    node = searchStation(root, id);

    if (node == NULL) {
        return APP_NOT_FOUND;
    }

    node->station = updatedStation;

    return APP_SUCCESS;
}

/* ---------- Minimum node helper ---------- */

static StationNode *findMinimumNode(StationNode *root)
{
    if (root == NULL) {
        return NULL;
    }

    while (root->left != NULL) {
        root = root->left;
    }

    return root;
}

/* ---------- AVL deletion ---------- */

static StationNode *deleteNode(
    StationNode *root,
    int id,
    AppStatus *status)
{
    StationNode *child;

    if (root == NULL) {
        *status = APP_NOT_FOUND;
        return NULL;
    }

    if (id < root->station.id) {
        StationNode *newLeft = deleteNode(
            root->left, id, status
        );

        if (*status == APP_NOT_FOUND) {
            return root;
        }

        root->left = newLeft;
    } else if (id > root->station.id) {
        StationNode *newRight = deleteNode(
            root->right, id, status
        );

        if (*status == APP_NOT_FOUND) {
            return root;
        }

        root->right = newRight;
    } else {
        /* Found the station to delete. */

        if (root->left == NULL || root->right == NULL) {
            child = (root->left != NULL)
                  ? root->left
                  : root->right;

            free(root);
            *status = APP_SUCCESS;

            return child;
        } else {
            /*
             * Two children:
             * Copy the inorder successor's station data,
             * then delete the successor from the right subtree.
             */
            StationNode *successor =
                findMinimumNode(root->right);

            root->station = successor->station;

            root->right = deleteNode(
                root->right,
                successor->station.id,
                status
            );
        }
    }

    if (*status != APP_SUCCESS) {
        return root;
    }

    return rebalance(root);
}

AppStatus deleteStation(StationNode **root, int id)
{
    AppStatus status = APP_SUCCESS;

    if (root == NULL || id <= 0) {
        return APP_INVALID_INPUT;
    }

    if (*root == NULL) {
        return APP_NOT_FOUND;
    }

    *root = deleteNode(*root, id, &status);

    return status;
}

/* ---------- Display ---------- */

static void printStationHeader(void)
{
    printf("\n");
    printf("%-12s %-30s %-12s %-12s %-10s\n",
           "Station ID",
           "Name",
           "Distance",
           "Fare",
           "Platform");

    printf("-------------------------------------------------------------------------------\n");
}

static void displayStationRows(const StationNode *root)
{
    if (root == NULL) {
        return;
    }

    /* Display the left subtree first. */
    displayStationRows(root->left);

    /* Display the current station using matching column widths. */
    printf("%-12d %-30s %-12.2f %-12.2f %-10d\n",
           root->station.id,
           root->station.name,
           root->station.distance,
           root->station.fare,
           root->station.platform);

    /* Display the right subtree. */
    displayStationRows(root->right);
}

void displayStation(const Station *station)
{
    if (station == NULL) {
        return;
    }

    /* Used when displaying one station from the search option. */
    printStationHeader();

    printf("%-12d %-30s %-12.2f %-12.2f %-10d\n",
           station->id,
           station->name,
           station->distance,
           station->fare,
           station->platform);
}

void displayStationsInOrder(const StationNode *root)
{
    if (root == NULL) {
        printf("\nNo stations available.\n");
        return;
    }

    /* Print the header exactly once for the entire table. */
    printStationHeader();

    /* Print every station in ascending station-ID order. */
    displayStationRows(root);

    printf("\n");
}

/* ---------- Statistics ---------- */

int countStations(const StationNode *root)
{
    if (root == NULL) {
        return 0;
    }

    return 1
         + countStations(root->left)
         + countStations(root->right);
}

int findMinimumStationId(const StationNode *root, int *id)
{
    if (root == NULL || id == NULL) {
        return 0;
    }

    while (root->left != NULL) {
        root = root->left;
    }

    *id = root->station.id;

    return 1;
}

int findMaximumStationId(const StationNode *root, int *id)
{
    if (root == NULL || id == NULL) {
        return 0;
    }

    while (root->right != NULL) {
        root = root->right;
    }

    *id = root->station.id;

    return 1;
}

/* ---------- Memory cleanup ---------- */

void freeStationTree(StationNode *root)
{
    if (root == NULL) {
        return;
    }

    freeStationTree(root->left);
    freeStationTree(root->right);

    free(root);
}
