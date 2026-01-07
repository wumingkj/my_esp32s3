// 全局JavaScript函数

// 密码可见性切换功能
function togglePasswordVisibility(inputId = 'password', toggleId = 'toggle_password') {
  const passwordInput = document.getElementById(inputId);
  const toggleButton = document.getElementById(toggleId);
  
  if (passwordInput.type === 'password') {
    passwordInput.type = 'text';
    toggleButton.textContent = '隐藏密码';
  } else {
    passwordInput.type = 'password';
    toggleButton.textContent = '显示密码';
  }
}

// 表单提交处理
function handleFormSubmit(formId, event) {
  event.preventDefault();
  const form = document.getElementById(formId);
  const formData = new FormData(form);
  
  fetch(form.action, {
    method: 'POST',
    body: formData
  })
  .then(response => response.json())
  .then(data => {
    if (data.status === 'success') {
      alert(data.message);
    } else {
      alert('错误: ' + data.message);
    }
  })
  .catch(error => {
    console.error('请求失败:', error);
    alert('请求失败: ' + error);
  });
  
  return false;
}

// 命令执行功能
function executeCommand(event) {
  event.preventDefault();
  const cmdInput = document.getElementById('cmd');
  const cmd = cmdInput.value.trim();
  
  if (cmd === '') {
    alert('请输入命令！');
    cmdInput.focus();
    return;
  }
  
  const submitButton = document.querySelector('input[type="submit"]');
  submitButton.disabled = true;
  submitButton.value = '执行中...';

  fetch('/command?cmd=' + encodeURIComponent(cmd), {
    method: 'GET'
  })
  .then(response => response.json())
  .then(data => {
    if (data.status === 'success') {
      alert(data.message);
    } else {
      alert('错误: ' + data.message);
    }
  })
  .catch(error => {
    alert('请求失败: ' + error);
  })
  .finally(() => {
    submitButton.disabled = false;
    submitButton.value = '执行命令';
    cmdInput.value = '';
    cmdInput.focus();
  });
}

// 仪表盘数据更新
function updateDashboard() {
  fetch('/update')
    .then(response => response.json())
    .then(data => {
      // 更新 ESP32 信息
      let esp32Info = '<table border="1" cellpadding="5" cellspacing="0">';
      esp32Info += '<tr><th>参数</th><th>值</th></tr>';
      esp32Info += '<tr><td>固件版本</td><td>' + data.firmware + '</td></tr>';
      esp32Info += '<tr><td>CPU频率</td><td>' + data.cpu_freq + ' MHz</td></tr>';
      esp32Info += '<tr><td>CPU0占用率</td><td>' + data.cpu0_usage.toFixed(2) + ' %</td></tr>';
      esp32Info += '<tr><td>CPU1占用率</td><td>' + data.cpu1_usage.toFixed(2) + ' %</td></tr>';
      esp32Info += '<tr><td>内部温度</td><td>' + data.internal_temp.toFixed(2) + ' ℃</td></tr>';
      esp32Info += '<tr><td>当前温度</td><td>' + data.current_temp + ' ℃</td></tr>';
      esp32Info += '<tr><td>当前湿度</td><td>' + data.current_humidity + ' %RH</td></tr>';
      esp32Info += '<tr><td>总内存</td><td>' + (data.total_memory / 1024).toFixed(2) + ' KB</td></tr>';
      esp32Info += '<tr><td>空闲内存</td><td>' + (data.free_memory / 1024).toFixed(2) + ' KB</td></tr>';
      esp32Info += '<tr><td>STA IP地址</td><td>' + data.sta_ip + '</td></tr>';
      esp32Info += '<tr><td>AP IP地址</td><td>' + data.ap_ip + '</td></tr>';
      esp32Info += '<tr><td>MAC地址</td><td>' + data.mac_address + '</td></tr>';
      esp32Info += '<tr><td>芯片型号</td><td>' + data.chip_model + '</td></tr>';
      esp32Info += '<tr><td>总闪存大小</td><td>' + (data.flash_size / 1024).toFixed(2) + ' KB</td></tr>';
      esp32Info += '<tr><td>剩余闪存空间</td><td>' + (data.flash_free / 1024).toFixed(2) + ' KB</td></tr>';
      esp32Info += '<tr><td>总PSRAM大小</td><td>' + (data.psram_size / 1024).toFixed(2) + ' KB</td></tr>';
      esp32Info += '<tr><td>空闲PSRAM</td><td>' + (data.free_psram / 1024).toFixed(2) + ' KB</td></tr>';
      esp32Info += '<tr><td>核心数</td><td>' + data.cores + '</td></tr>';
      esp32Info += '<tr><td>运行时间</td><td>' + data.uptime + ' 秒</td></tr>';
      esp32Info += '</table>';
      
      const esp32InfoElement = document.getElementById('esp32Info');
      if (esp32InfoElement) {
        esp32InfoElement.innerHTML = esp32Info;
      }

      // 更新 MAC 列表
      let macList = '<h2>连接的设备列表</h2>';
      macList += '<table border="1" cellpadding="5" cellspacing="0">';
      macList += '<tr><th>序号</th><th>IP地址</th><th>MAC地址</th></tr>';
      
      data.connected_devices.forEach(function(device, index) {
        macList += '<tr><td>' + (index + 1) + '</td><td>' + device.ip + '</td><td>' + device.mac + '</td></tr>';
      });
      
      macList += '</table>';
      
      const macListElement = document.getElementById('macList');
      if (macListElement) {
        macListElement.innerHTML = macList;
      }
    })
    .catch(error => {
      console.error('更新仪表盘失败:', error);
    });
}

// 舵机控制功能
let debounceTimer;
const DEBOUNCE_DELAY = 150;

function handleAngleChange(value) {
  const angle = parseInt(value);
  updateAngleIndicator(angle);
  
  clearTimeout(debounceTimer);
  debounceTimer = setTimeout(() => {
    sendAngleToESP32(angle);
  }, DEBOUNCE_DELAY);
}

function updateAngleIndicator(angle) {
  const slider = document.getElementById('angleSlider');
  const indicator = document.getElementById('angleIndicator');
  
  if (slider && indicator) {
    const rangeWidth = slider.offsetWidth;
    const thumbPosition = (angle / 180) * rangeWidth;
    indicator.style.left = `${thumbPosition}px`;
    
    const angleValueElement = document.getElementById('angleValue');
    if (angleValueElement) {
      angleValueElement.textContent = angle;
    }
  }
}

function sendAngleToESP32(angle) {
  fetch('/servo-control', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: `angle=${angle}`
  }).catch(error => {
    console.error('控制请求失败:', error);
  });
}

// RGB控制功能
function toggleCustomMode() {
  const modeSelect = document.getElementById('rgbModeSelect');
  const customOptions = document.getElementById('customColorOptions');
  
  if (modeSelect && customOptions) {
    if (modeSelect.value === 'Custom') {
      customOptions.style.display = 'block';
    } else {
      customOptions.style.display = 'none';
    }
  }
}

function addUniqueColor() {
  const container = document.getElementById('uniqueColorsContainer');
  if (!container) return;
  
  const index = container.children.length;
  const uniqueColorDiv = document.createElement('div');
  uniqueColorDiv.innerHTML = `
    <label for="uniqueColor${index}">LED ${index} 颜色:</label>
    <input type="number" id="uniqueColor${index}_index" name="uniqueColor${index}_index" placeholder="灯珠编号" min="0" required>
    <input type="color" id="uniqueColor${index}_picker" onchange="updateColorInput(${index})">
    <input type="text" id="uniqueColor${index}_manual" name="uniqueColor${index}" placeholder="例如: FFFFFF 或 #FFFFFF" readonly>
    <button type="button" onclick="removeUniqueColor(${index})">删除</button>
    <br>
  `;
  container.appendChild(uniqueColorDiv);
}

function updateColorInput(index) {
  const colorPicker = document.getElementById(`uniqueColor${index}_picker`);
  const colorInput = document.getElementById(`uniqueColor${index}_manual`);
  
  if (colorPicker && colorInput) {
    colorInput.value = colorPicker.value.toUpperCase().replace('#', '');
  }
}

function removeUniqueColor(index) {
  const container = document.getElementById('uniqueColorsContainer');
  if (!container) return;
  
  const uniqueColorDiv = container.querySelector(`div:nth-child(${index + 1})`);
  if (uniqueColorDiv) {
    container.removeChild(uniqueColorDiv);
  }
}

// TFT控制功能
function updateDisplayMode() {
  const modeSelect = document.getElementById('modeSelect');
  if (!modeSelect) return;
  
  const mode = modeSelect.value;
  fetch('/set-display-mode?mode=' + mode, {
    method: 'GET'
  })
  .then(response => response.json())
  .then(result => {
    updateFormVisibility(mode);
  })
  .catch(error => {
    console.error('Error:', error);
  });
}

function updateFormVisibility(mode) {
  const customContentForm = document.getElementById('customContentForm');
  const uploadImageForm = document.getElementById('uploadImageForm');
  const tftMenuControl = document.getElementById('tftMenuControl');
  
  if (customContentForm && uploadImageForm && tftMenuControl) {
    if (mode === '0') {
      customContentForm.style.display = 'none';
      uploadImageForm.style.display = 'none';
      tftMenuControl.style.display = 'block';
    } else if (mode === '1') {
      customContentForm.style.display = 'block';
      uploadImageForm.style.display = 'none';
      tftMenuControl.style.display = 'none';
    } else if (mode === '2') {
      customContentForm.style.display = 'none';
      uploadImageForm.style.display = 'block';
      tftMenuControl.style.display = 'none';
    } else {
      customContentForm.style.display = 'none';
      uploadImageForm.style.display = 'none';
      tftMenuControl.style.display = 'none';
    }
  }
}

function handleBrightnessChange(event) {
  const brightness = event.target.value;
  fetch('/tft-control', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'brightness=' + encodeURIComponent(brightness)
  }).catch(error => {
    console.error('Error:', error);
  });
}

function sendMenuCommand(command) {
  fetch('/tft-menu-control', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'command=' + encodeURIComponent(command)
  }).catch(error => {
    console.error('Error:', error);
  });
}

// 网络设置功能
function updateAPNetwork() {
  const formData = new FormData(document.getElementById('apForm'));
  fetch('/update-ap-network', {
    method: 'POST',
    body: formData
  }).then(response => response.json()).then(data => {
    alert(data.message);
  }).catch(error => {
    alert('更新失败: ' + error);
  });
}

function addSTA() {
  const formData = new FormData(document.getElementById('staForm'));
  fetch('/add-sta', {
    method: 'POST',
    body: formData
  }).then(response => response.json()).then(data => {
    alert(data.message);
  }).catch(error => {
    alert('添加失败: ' + error);
  });
}

function deleteSTA(ssid) {
  fetch('/delete-sta?ssid=' + ssid, {
    method: 'GET'
  }).then(response => response.json()).then(data => {
    alert(data.message);
  }).catch(error => {
    alert('删除失败: ' + error);
  });
}

function updateSTA(ssid, password) {
  const newSSID = prompt('请输入新的SSID:', ssid);
  const newPassword = prompt('请输入新的密码:', password);
  
  if (newSSID && newPassword) {
    fetch('/update-sta-network', {
      method: 'POST',
      body: new URLSearchParams('sta_ssid=' + encodeURIComponent(newSSID) + '&sta_password=' + encodeURIComponent(newPassword))
    }).then(response => response.json()).then(data => {
      alert(data.message);
    }).catch(error => {
      alert('更新失败: ' + error);
    });
  }
}

// 备份恢复功能
function downloadBackup() {
  window.location.href = '/backup';
}

function factoryReset() {
  fetch('/factory-reset', {
    method: 'POST'
  })
  .then(response => response.text())
  .then(result => {
    alert('恢复出厂设置成功: ' + result);
  })
  .catch(error => {
    console.error('Error:', error);
  });
}

// 页面加载完成后的初始化
document.addEventListener('DOMContentLoaded', function() {
  // 初始化舵机指示器位置
  updateAngleIndicator(90);
  
  // 初始化RGB模式显示
  toggleCustomMode();
  
  // 初始化TFT显示模式
  const initialMode = parseInt(new URLSearchParams(window.location.search).get('mode') || 0);
  const modeSelect = document.getElementById('modeSelect');
  if (modeSelect) {
    modeSelect.value = initialMode;
    updateFormVisibility(initialMode);
  }
  
  // 设置仪表盘自动更新（如果存在）
  const esp32InfoElement = document.getElementById('esp32Info');
  if (esp32InfoElement) {
    setInterval(updateDashboard, 2000);
    updateDashboard();
  }
  
  // 命令输入框回车键支持
  const cmdInput = document.getElementById('cmd');
  if (cmdInput) {
    cmdInput.addEventListener('keypress', function(event) {
      if (event.key === 'Enter') {
        executeCommand(event);
      }
    });
  }
});