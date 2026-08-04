.pragma library

// Main-axis distribution, shared by Row and Col.
//
// Only the arithmetic lives here, not the property plumbing: Row writes `x`
// and Col writes `y`, and passing those names in as strings would trade two
// readable loops for one that cannot be read at all. What the two genuinely
// share is this -- and it is the part with the edge cases.

// Absolute leading offsets for `sizes`, laid out along an axis of `available`
// length with `spacing` between neighbours.
//
// Returns null when there is nothing to do, so a caller can skip the write
// pass entirely rather than assigning every child its current position.
function distribute(sizes, available, spacing, justify) {
    var count = sizes.length;
    if (count === 0 || justify === 0)
        return null;

    var used = (count - 1) * spacing;
    for (var i = 0; i < count; ++i)
        used += sizes[i];

    // Overflowing is not a distribution problem. Packing to the start is what
    // the positioner would have done anyway, and spreading negative free space
    // would move children *backwards* past the container's edge.
    var free = available - used;
    if (free <= 0)
        return null;

    var lead = 0;
    var gap = spacing;
    switch (justify) {
    case 1: // Center
        lead = free / 2;
        break;
    case 2: // End
        lead = free;
        break;
    case 3: // SpaceBetween
        // One child has no gap to distribute into; leaving it at the start is
        // what every layout engine does here.
        if (count > 1)
            gap = spacing + free / (count - 1);
        break;
    case 4: // SpaceAround
        lead = free / (2 * count);
        gap = spacing + free / count;
        break;
    case 5: // SpaceEvenly
        lead = free / (count + 1);
        gap = spacing + free / (count + 1);
        break;
    default:
        return null;
    }

    var offsets = [];
    var cursor = lead;
    for (var j = 0; j < count; ++j) {
        offsets.push(cursor);
        cursor += sizes[j] + gap;
    }
    return offsets;
}
