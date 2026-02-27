/*
 * tiki_skel.c -- Skeleton loading and management
 *
 * Loads .skl skeleton files that define bone hierarchies.
 * Each skeleton has a tree of bones with parent relationships,
 * bind poses, and name identifiers for scripting access.
 */

#include "tiki.h"
#include "../common/qcommon.h"

/* TODO: Implement skeleton loader
 *
 * .skl file format (needs reverse engineering from pak0.pk3 assets):
 * - Header with bone count
 * - Bone definitions (name, parent index, bind pose transform)
 * - Bone hierarchy
 */
