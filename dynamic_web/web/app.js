function formatUptime(ms) {
  const totalSeconds = Math.floor(ms / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return `${hours}h ${minutes}m ${seconds}s`;
}

function updateLoadBar(percent, barId, labelId) {
  const bar = document.getElementById(barId);
  const label = document.getElementById(labelId);

  if (percent >= 0) {
    const clamped = Math.min(100, Math.max(0, percent));
    bar.style.width = `${clamped}%`;
    bar.textContent = `${clamped}%`;
    label.textContent = `${clamped}%`;
    bar.setAttribute('aria-valuenow', String(clamped));
  } else {
    bar.style.width = '0%';
    bar.textContent = 'N/A';
    label.textContent = 'N/A';
    bar.setAttribute('aria-valuenow', '0');
  }
}

async function refreshStatus() {
  try {
    const response = await fetch('/api/status', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();
    const cpuLoad = Number(data.cpu_load_percent ?? -1);
    const ramLoad = Number(data.ram_util_percent ?? -1);

    document.getElementById('ip').textContent = data.ip ?? '-';
    document.getElementById('ssid').textContent = data.ssid ?? '-';
    document.getElementById('uptime').textContent = formatUptime(Number(data.uptime_ms ?? 0));
    updateLoadBar(cpuLoad, 'cpu-load-bar', 'cpu-load-label');
    updateLoadBar(ramLoad, 'ram-load-bar', 'ram-load-label');
  } catch (error) {
    document.getElementById('ip').textContent = 'Unavailable';
    document.getElementById('ssid').textContent = 'Unavailable';
    document.getElementById('uptime').textContent = error.message;
    updateLoadBar(-1, 'cpu-load-bar', 'cpu-load-label');
    updateLoadBar(-1, 'ram-load-bar', 'ram-load-label');
  }
}

refreshStatus();
setInterval(refreshStatus, 2000);
