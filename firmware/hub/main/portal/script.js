async function loadStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        document.getElementById('hubNameDisplay').textContent = data.hub_name || 'Sin Nombre';
        document.getElementById('ipAddr').textContent = data.ip || 'No conectado';
        document.getElementById('mqttStatus').innerHTML = data.mqtt_connected ? 
            '<span class="status-ok">✅ Conectado</span>' : '<span class="status-err">❌ Desconectado</span>';
        document.getElementById('nodeCount').textContent = data.node_count || 0;

        const tbody = document.getElementById('nodesBody');
        tbody.innerHTML = '';

        if (!data.nodes || data.nodes.length === 0) {
            tbody.innerHTML = '<tr><td colspan="8">No hay nodos vinculados. Acerca un nodo nuevo para emparejar.</td></tr>';
            return;
        }

        const now = Math.floor(Date.now() / 1000);

        data.nodes.forEach(node => {
            const secondsAgo = node.last_ping > 0 ? (now - node.last_ping) : 9999;
            const statusText = secondsAgo < 120 ? '🟢 Activo' : (secondsAgo < 3600 ? '🟡 Inactivo' : '🔴 Offline');
            
            const row = document.createElement('tr');
            row.innerHTML = `
                <td><code>${node.mac}</code></td>
                <td>${node.type}</td>
                <td><input type="text" id="name_${node.mac}" value="${node.name}" placeholder="Ej: Sensor Garaje"></td>
                <td><input type="text" id="loc_${node.mac}" value="${node.location}" placeholder="Ej: Casa"></td>
                <td><input type="text" id="room_${node.mac}" value="${node.room}" placeholder="Ej: Garaje"></td>
                <td><input type="number" id="thresh_${node.mac}" value="${node.distance_threshold}" min="2" max="200" style="width: 60px;"></td>
                <td>${statusText} <br><small>${secondsAgo < 9999 ? 'Hace ' + secondsAgo + 's' : 'Nunca'}</small></td>
                <td>
                    <button class="btn btn-save" onclick="saveNode('${node.mac}')">💾</button>
                    <button class="btn btn-del" onclick="deleteNode('${node.mac}')">🗑️</button>
                </td>
            `;
            tbody.appendChild(row);
        });
    } catch (err) {
        console.error("Error cargando estado:", err);
        document.getElementById('nodesBody').innerHTML = '<tr><td colspan="8">Error al cargar datos.</td></tr>';
    }
}

async function saveNode(mac) {
    const payload = {
        mac: mac,
        name: document.getElementById(`name_${mac}`).value,
        location: document.getElementById(`loc_${mac}`).value,
        room: document.getElementById(`room_${mac}`).value,
        distance_threshold: parseInt(document.getElementById(`thresh_${mac}`).value) || 50
    };

    try {
        const res = await fetch('/api/nodes', {
            method: 'PUT',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(payload)
        });
        const result = await res.json();
        if (result.status === 'ok') {
            alert('✅ Nodo actualizado correctamente');
            loadStatus(); // Recargar para ver cambios
        } else {
            alert('❌ Error: ' + result.message);
        }
    } catch (err) {
        alert('❌ Error de red: ' + err.message);
    }
}

async function deleteNode(mac) {
    if (!confirm('¿Estás seguro de eliminar este nodo?')) return;
    try {
        const res = await fetch(`/api/nodes?mac=${mac}`, { method: 'DELETE' });
        const result = await res.json();
        if (result.status === 'ok') {
            alert('✅ Nodo eliminado');
            loadStatus();
        } else {
            alert('❌ Error: ' + result.message);
        }
    } catch (err) {
        alert('❌ Error de red: ' + err.message);
    }
}

// Cargar al iniciar y refrescar cada 10 segundos
loadStatus();
setInterval(loadStatus, 10000);
