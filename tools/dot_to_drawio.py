"""
dot_to_drawio.py
================
Parses the DOT source embedded in docs/callgraph.md and writes docs/callgraph.drawio.

Layout strategy
---------------
Nodes are arranged in columns based on BFS depth from 'main'.
Interrupt-cluster nodes are placed in a separate column group below the main graph.
Each column is stacked vertically; columns are spaced horizontally.

Usage:
    python tools/dot_to_drawio.py
"""

import re
import xml.etree.ElementTree as ET
from collections import defaultdict, deque

# ---------------------------------------------------------------------------
# 1. Parse DOT from the markdown file
# ---------------------------------------------------------------------------

DOT_FENCE_RE = re.compile(r'```dot\s*(.*?)```', re.DOTALL)

# "a -> b" or  'a -> b [label="..."]'  or  'a -> b [style=dashed label="..."]'
EDGE_RE = re.compile(
    r'(\w+)\s*->\s*(\w+)'
    r'(?:\s*\[([^\]]*)\])?'
)

# node attribute line:  "node_name [shape=ellipse];"  or  "node_name [label="..."];"
NODE_ATTR_RE = re.compile(
    r'^(\w+)\s*\[([^\]]*)\]\s*;?$'
)

LABEL_RE  = re.compile(r'label\s*=\s*"([^"]*)"')
STYLE_RE  = re.compile(r'style\s*=\s*(\w+)')
SHAPE_RE  = re.compile(r'shape\s*=\s*(\w+)')

INTERRUPT_NODES = {
    'SysTick_Handler',
    'EXTI_DW1000_IRQ',
    'USART_IRQ',
    'dwt_isr',
    'net_rx_ok_isr',
    'net_rx_to_isr',
    'net_rx_err_isr',
    'ss_twr_isr',
    'rx_rbuf_push',
}

# Nodes that are deca library internals — rendered with a different fill
DECA_NODES = {n for n in [
    'dwt_initialise', 'dwt_configure', 'dwt_starttx', 'dwt_rxenable',
    'dwt_readrxdata', 'dwt_readrxtimestamp', 'dwt_setdelayedtrxtime',
    'dwt_writetxdata', 'dwt_writetxfctrl', 'dwt_read32bitreg',
    'dwt_write32bitreg', 'dwt_readcarrierintegrator',
    'dwt_forcetrxoff', 'dwt_rxreset', 'dwt_setrxaftertxdelay',
    'dwt_setrxtimeout', 'dwt_readtxtimestamplo32', 'dwt_readrxtimestamplo32',
    'dwt_settxantennadelay', 'dwt_setrxantennadelay',
    'dwt_readtempvbat', 'dwt_geticreftemp',
    'dwt_isr',
    'dwt_setpanid', 'dwt_seteui', 'dwt_setaddress16', 'dwt_enableframefilter',
    'dwt_setleds',
    'decamutexon', 'decamutexoff',
]}


def parse_dot(md_path: str):
    with open(md_path, encoding='utf-8') as f:
        text = f.read()

    match = DOT_FENCE_RE.search(text)
    if not match:
        raise ValueError("No ```dot block found in " + md_path)
    dot_src = match.group(1)

    nodes: dict[str, dict] = {}   # name -> attrs
    edges: list[dict] = []
    in_cluster = False

    for raw_line in dot_src.splitlines():
        line = raw_line.strip()

        # track interrupt cluster
        if 'cluster_interrupts' in line:
            in_cluster = True
        if in_cluster and line == '}':
            in_cluster = False

        # skip comments and directives
        if not line or line.startswith('//') or line.startswith('/*') or line.startswith('*'):
            continue
        if line.startswith('digraph') or line.startswith('rankdir') or \
           line.startswith('node [') or line.startswith('edge [') or \
           line.startswith('subgraph') or line.startswith('label=') or \
           line.startswith('style=') or line.startswith('color=') or \
           line == '{' or line == '}':
            continue

        # edge
        em = EDGE_RE.search(line)
        if em:
            src, dst, attrs_str = em.group(1), em.group(2), em.group(3) or ''
            label = (LABEL_RE.search(attrs_str) or type('', (), {'group': lambda s, n: ''})()).group(1) if attrs_str else ''
            dashed = 'dashed' in attrs_str
            edge = {'src': src, 'dst': dst, 'label': label, 'dashed': dashed,
                    'in_cluster': in_cluster}
            edges.append(edge)
            # register nodes
            for n in (src, dst):
                if n not in nodes:
                    nodes[n] = {'in_cluster': in_cluster or n in INTERRUPT_NODES}
            if in_cluster:
                nodes[src]['in_cluster'] = True
                nodes[dst]['in_cluster'] = True
            continue

        # standalone node attribute
        nm = NODE_ATTR_RE.match(line.rstrip(';'))
        if nm:
            name, attrs_str = nm.group(1), nm.group(2)
            if name not in nodes:
                nodes[name] = {}
            sm = SHAPE_RE.search(attrs_str)
            lm = LABEL_RE.search(attrs_str)
            if sm:
                nodes[name]['shape'] = sm.group(1)
            if lm:
                nodes[name]['display_label'] = lm.group(1)

    # mark known interrupt nodes
    for n in list(nodes):
        if n in INTERRUPT_NODES:
            nodes[n]['in_cluster'] = True

    return nodes, edges


# ---------------------------------------------------------------------------
# 2. Layout: BFS depth → columns
# ---------------------------------------------------------------------------

def compute_layout(nodes: dict, edges: list):
    # Build adjacency (main graph only)
    adj = defaultdict(list)
    for e in edges:
        if not e['in_cluster']:
            adj[e['src']].append(e['dst'])

    # BFS from 'main'
    depth = {}
    queue = deque(['main'])
    depth['main'] = 0
    while queue:
        node = queue.popleft()
        for nxt in adj[node]:
            if nxt not in depth and not nodes.get(nxt, {}).get('in_cluster'):
                depth[nxt] = depth[node] + 1
                queue.append(nxt)

    # Nodes that BFS didn't reach (islands in the main graph)
    max_depth = max(depth.values()) if depth else 0
    for n, attrs in nodes.items():
        if n not in depth and not attrs.get('in_cluster'):
            depth[n] = max_depth + 1

    # Group by depth
    columns: dict[int, list] = defaultdict(list)
    for n, d in depth.items():
        columns[d].append(n)

    # Interrupt nodes go to a separate section
    interrupt_nodes = [n for n, a in nodes.items() if a.get('in_cluster')]

    return depth, columns, interrupt_nodes


# ---------------------------------------------------------------------------
# 3. Assign pixel positions
# ---------------------------------------------------------------------------

NODE_W = 160
NODE_H = 40
COL_GAP = 60    # horizontal gap between columns
ROW_GAP = 14    # vertical gap between rows in same column
IRQ_Y_OFFSET = 60  # extra vertical gap before interrupt section


def assign_positions(columns: dict, interrupt_nodes: list):
    positions = {}

    # sort columns by depth
    sorted_depths = sorted(columns)

    # compute x for each column
    col_x = {}
    x = 20
    for d in sorted_depths:
        col_x[d] = x
        x += NODE_W + COL_GAP

    # assign y within each column
    for d in sorted_depths:
        col_nodes = sorted(columns[d])  # stable alphabetical order within column
        y = 20
        for n in col_nodes:
            positions[n] = (col_x[d], y)
            y += NODE_H + ROW_GAP

    # interrupt nodes: stacked to the right of everything, with y offset
    irq_x = x
    y = 20 + IRQ_Y_OFFSET
    for n in sorted(interrupt_nodes):
        positions[n] = (irq_x, y)
        y += NODE_H + ROW_GAP

    return positions


# ---------------------------------------------------------------------------
# 4. Build draw.io XML
# ---------------------------------------------------------------------------

FILL_NORMAL   = '#dae8fc'   # light blue
FILL_DECA     = '#fff2cc'   # light yellow — deca library
FILL_ENTRY    = '#d5e8d4'   # light green — main()
FILL_IRQ      = '#ffe6cc'   # light orange — interrupts
STROKE_NORMAL = '#6c8ebf'
STROKE_DECA   = '#d6b656'
STROKE_ENTRY  = '#82b366'
STROKE_IRQ    = '#d79b00'


def make_cell_id(prefix: str, idx: int) -> str:
    return f"{prefix}_{idx}"


def build_drawio(nodes: dict, edges: list, positions: dict) -> str:
    # root
    mxfile = ET.Element('mxfile', {'host': 'dot_to_drawio.py', 'version': '21.0.0'})
    diagram = ET.SubElement(mxfile, 'diagram', {'name': 'Call Graph'})
    mxgraph = ET.SubElement(diagram, 'mxGraphModel', {
        'dx': '1422', 'dy': '762', 'grid': '1', 'gridSize': '10',
        'guides': '1', 'tooltips': '1', 'connect': '1', 'arrows': '1',
        'fold': '1', 'page': '0', 'pageScale': '1',
        'pageWidth': '1654', 'pageHeight': '1169',
        'math': '0', 'shadow': '0',
    })
    root = ET.SubElement(mxgraph, 'root')
    ET.SubElement(root, 'mxCell', {'id': '0'})
    ET.SubElement(root, 'mxCell', {'id': '1', 'parent': '0'})

    node_id: dict[str, str] = {}
    cell_idx = 2

    # --- node cells ---
    for name, attrs in nodes.items():
        if name not in positions:
            continue
        x, y = positions[name]
        cell_id = make_cell_id('n', cell_idx)
        node_id[name] = cell_id
        cell_idx += 1

        # style
        is_irq   = attrs.get('in_cluster', False) or name in INTERRUPT_NODES
        is_deca  = name in DECA_NODES
        is_entry = (attrs.get('shape') == 'ellipse') or name == 'main'

        if is_irq:
            fill, stroke, shape_extra = FILL_IRQ, STROKE_IRQ, ''
        elif is_deca:
            fill, stroke, shape_extra = FILL_DECA, STROKE_DECA, ''
        elif is_entry:
            fill, stroke, shape_extra = FILL_ENTRY, STROKE_ENTRY, 'ellipse;'
        else:
            fill, stroke, shape_extra = FILL_NORMAL, STROKE_NORMAL, ''

        style = (
            f'rounded=1;whiteSpace=wrap;html=1;'
            f'{shape_extra}'
            f'fillColor={fill};strokeColor={stroke};'
            f'fontFamily=Courier New;fontSize=9;'
        )
        display = attrs.get('display_label', name)

        cell = ET.SubElement(root, 'mxCell', {
            'id': cell_id,
            'value': display,
            'style': style,
            'vertex': '1',
            'parent': '1',
        })
        ET.SubElement(cell, 'mxGeometry', {
            'x': str(x), 'y': str(y),
            'width': str(NODE_W), 'height': str(NODE_H),
            'as': 'geometry',
        })

    # --- interrupt group label ---
    irq_positions = [(x, y) for name, (x, y) in positions.items()
                     if nodes.get(name, {}).get('in_cluster') or name in INTERRUPT_NODES]
    if irq_positions:
        irq_x_min = min(p[0] for p in irq_positions) - 10
        irq_y_min = min(p[1] for p in irq_positions) - 30
        irq_x_max = max(p[0] for p in irq_positions) + NODE_W + 10
        irq_y_max = max(p[1] for p in irq_positions) + NODE_H + 10

        label_cell_id = make_cell_id('n', cell_idx)
        cell_idx += 1
        lc = ET.SubElement(root, 'mxCell', {
            'id': label_cell_id,
            'value': 'Interrupt handlers',
            'style': (
                'swimlane;startSize=20;fillColor=#fff2cc;strokeColor=#d79b00;'
                'fontStyle=1;fontSize=10;'
            ),
            'vertex': '1',
            'parent': '1',
        })
        ET.SubElement(lc, 'mxGeometry', {
            'x': str(irq_x_min), 'y': str(irq_y_min),
            'width': str(irq_x_max - irq_x_min),
            'height': str(irq_y_max - irq_y_min),
            'as': 'geometry',
        })

    # --- edge cells ---
    for e in edges:
        src, dst = e['src'], e['dst']
        if src not in node_id or dst not in node_id:
            continue
        edge_id = make_cell_id('e', cell_idx)
        cell_idx += 1

        style = 'edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;exitX=1;exitY=0.5;exitDx=0;exitDy=0;entryX=0;entryY=0.5;entryDx=0;entryDy=0;'
        if e['dashed']:
            style += 'dashed=1;'

        ec = ET.SubElement(root, 'mxCell', {
            'id': edge_id,
            'value': e['label'],
            'style': style,
            'edge': '1',
            'source': node_id[src],
            'target': node_id[dst],
            'parent': '1',
        })
        ET.SubElement(ec, 'mxGeometry', {'relative': '1', 'as': 'geometry'})

    # serialise
    ET.indent(mxfile, space='  ')
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(mxfile, encoding='unicode')


# ---------------------------------------------------------------------------
# 5. Main
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    import os
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root  = os.path.dirname(script_dir)

    md_path  = os.path.join(repo_root, 'docs', 'callgraph.md')
    out_path = os.path.join(repo_root, 'docs', 'callgraph.drawio')

    print(f'Parsing {md_path} ...')
    nodes, edges = parse_dot(md_path)
    print(f'  {len(nodes)} nodes, {len(edges)} edges')

    depth, columns, interrupt_nodes = compute_layout(nodes, edges)
    print(f'  BFS depth max = {max(depth.values()) if depth else 0}')
    print(f'  Interrupt nodes: {len(interrupt_nodes)}')

    positions = assign_positions(columns, interrupt_nodes)

    xml = build_drawio(nodes, edges, positions)

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(xml)

    print(f'Written: {out_path}')
    print('Open in draw.io (app.diagrams.net) or the desktop app.')
