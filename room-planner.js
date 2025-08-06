// Room Planner JavaScript

class RoomPlanner {
  constructor() {
    this.furnitureData = this.initializeFurnitureData();
    this.roomSettings = {
      width: 4.0,
      length: 4.0,
      height: 2.7
    };
    this.placedFurniture = [];
    this.selectedFurniture = null;
    this.currentView = 'top'; // Default to top view
    
    this.init();
  }

  init() {
    this.renderFurnitureGrid();
    this.bindEvents();
    this.updateRoomDisplay();
    this.updateRoomSizeDisplay();
    this.updateStats();
  }

  initializeFurnitureData() {
    return [
      {
        id: 'cabinet_1',
        name: 'ארון בגדים',
        category: 'storage',
        dimensions: { width: 120, depth: 60, height: 200 },
        iconClass: 'cabinet',
        description: 'ארון בגדים דו-דלתי'
      },
      {
        id: 'cabinet_2',
        name: 'ארון נעליים',
        category: 'storage',
        dimensions: { width: 80, depth: 30, height: 120 },
        iconClass: 'cabinet',
        description: 'ארון נעליים קומפקטי'
      },
      {
        id: 'drawer_1',
        name: 'שידת מגירות',
        category: 'storage',
        dimensions: { width: 80, depth: 40, height: 85 },
        iconClass: 'drawer',
        description: 'שידה עם 4 מגירות'
      },
      {
        id: 'drawer_2',
        name: 'קומודה',
        category: 'storage',
        dimensions: { width: 120, depth: 45, height: 80 },
        iconClass: 'drawer',
        description: 'קומודה עם 6 מגירות'
      },
      {
        id: 'table_1',
        name: 'שולחן כתיבה',
        category: 'tables',
        dimensions: { width: 120, depth: 60, height: 75 },
        iconClass: 'table',
        description: 'שולחן עבודה מודרני'
      },
      {
        id: 'table_2',
        name: 'שולחן אוכל',
        category: 'tables',
        dimensions: { width: 160, depth: 90, height: 75 },
        iconClass: 'table',
        description: 'שולחן אוכל ל-6 אנשים'
      },
      {
        id: 'table_3',
        name: 'שולחן קפה',
        category: 'tables',
        dimensions: { width: 100, depth: 60, height: 45 },
        iconClass: 'table',
        description: 'שולחן סלון נמוך'
      },
      {
        id: 'chair_1',
        name: 'כיסא משרד',
        category: 'seating',
        dimensions: { width: 60, depth: 60, height: 120 },
        iconClass: 'chair',
        description: 'כיסא ארגונומי'
      },
      {
        id: 'chair_2',
        name: 'כורסא',
        category: 'seating',
        dimensions: { width: 80, depth: 85, height: 95 },
        iconClass: 'chair',
        description: 'כורסא נוחה'
      },
      {
        id: 'bed_1',
        name: 'מיטה זוגית',
        category: 'bedroom',
        dimensions: { width: 160, depth: 200, height: 100 },
        iconClass: 'table',
        description: 'מיטה זוגית 160x200'
      },
      {
        id: 'bed_2',
        name: 'מיטה יחיד',
        category: 'bedroom',
        dimensions: { width: 90, depth: 200, height: 100 },
        iconClass: 'table',
        description: 'מיטה יחיד 90x200'
      },
      {
        id: 'nightstand_1',
        name: 'שידת לילה',
        category: 'bedroom',
        dimensions: { width: 45, depth: 35, height: 60 },
        iconClass: 'drawer',
        description: 'שידת לילה קומפקטית'
      }
    ];
  }

  renderFurnitureGrid() {
    const grid = document.getElementById('furnitureGrid');
    grid.innerHTML = '';

    const activeCategory = document.querySelector('.category-btn.active').dataset.category;
    const searchTerm = document.getElementById('searchInput').value.toLowerCase();

    const filteredFurniture = this.furnitureData.filter(item => {
      const matchesCategory = activeCategory === 'all' || item.category === activeCategory;
      const matchesSearch = item.name.toLowerCase().includes(searchTerm) || 
                           item.description.toLowerCase().includes(searchTerm);
      return matchesCategory && matchesSearch;
    });

    filteredFurniture.forEach(item => {
      const furnitureElement = this.createFurnitureElement(item);
      grid.appendChild(furnitureElement);
    });
  }

  createFurnitureElement(item) {
    const element = document.createElement('div');
    element.className = 'furniture-item';
    element.dataset.furnitureId = item.id;

    element.innerHTML = `
      <div class="furniture-icon ${item.iconClass}"></div>
      <h4>${item.name}</h4>
      <div class="dimensions">${item.dimensions.width}×${item.dimensions.depth}×${item.dimensions.height} ס"מ</div>
      <button class="add-btn" onclick="roomPlanner.addFurnitureToRoom('${item.id}')">הוסף לחדר</button>
    `;

    return element;
  }

  addFurnitureToRoom(furnitureId) {
    const furniture = this.furnitureData.find(item => item.id === furnitureId);
    if (!furniture) return;

    // Check if furniture fits in room
    const roomWidthCm = this.roomSettings.width * 100;
    const roomLengthCm = this.roomSettings.length * 100;

    if (furniture.dimensions.width > roomWidthCm || furniture.dimensions.depth > roomLengthCm) {
      alert(`הפריט גדול מדי עבור החדר הנוכחי!\nגודל הפריט: ${furniture.dimensions.width}×${furniture.dimensions.depth} ס"מ\nגודל החדר: ${roomWidthCm}×${roomLengthCm} ס"מ`);
      return;
    }

    // Find a good starting position
    let positionX, positionY;
    
    if (this.currentView === 'front') {
      // In front view, place furniture sequentially from left to right
      const existingFurnitureCount = this.placedFurniture.length;
      const spacing = furniture.dimensions.width + 20; // 20cm gap between items
      positionX = existingFurnitureCount * spacing;
      positionY = 0; // Always on the floor in front view
      
      // If goes beyond room width, wrap to next position
      if (positionX + furniture.dimensions.width > roomWidthCm) {
        positionX = 0;
      }
    } else {
      // Top view - use center with randomness
      const centerX = (roomWidthCm - furniture.dimensions.width) / 2;
      const centerY = (roomLengthCm - furniture.dimensions.depth) / 2;
      const randomOffsetX = (Math.random() - 0.5) * 50; // ±25cm random offset
      const randomOffsetY = (Math.random() - 0.5) * 50;
      
      positionX = Math.max(0, Math.min(centerX + randomOffsetX, roomWidthCm - furniture.dimensions.width));
      positionY = Math.max(0, Math.min(centerY + randomOffsetY, roomLengthCm - furniture.dimensions.depth));
    }

    // Create placed furniture object
    const placedItem = {
      id: `placed_${Date.now()}`,
      furnitureId: furnitureId,
      furniture: furniture,
      position: {
        x: positionX,
        y: positionY,
        rotation: 0
      },
      manuallyPositioned: false // Mark as not manually positioned initially
    };

    this.placedFurniture.push(placedItem);
    this.renderRoomFurniture();
    this.updateFurnitureList();
    this.updateStats();
    this.showAddedFeedback(furniture.name);
  }

  showAddedFeedback(name) {
    // Create temporary feedback element
    const feedback = document.createElement('div');
    feedback.style.cssText = `
      position: fixed;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      background: var(--planner-success);
      color: white;
      padding: 15px 25px;
      border-radius: 8px;
      font-weight: 600;
      z-index: 10000;
      animation: fadeInOut 2s ease-out forwards;
    `;
    feedback.textContent = `✓ ${name} נוסף לחדר`;
    
    document.body.appendChild(feedback);
    
    setTimeout(() => {
      document.body.removeChild(feedback);
    }, 2000);
  }

  renderRoomFurniture() {
    const container = document.getElementById('furnitureContainer');
    
    // Store current positions before clearing
    const currentPositions = {};
    const existingElements = container.querySelectorAll('.room-furniture');
    existingElements.forEach(element => {
      const placedId = element.dataset.placedId;
      if (placedId && element.style.left && element.style.top) {
        currentPositions[placedId] = {
          left: element.style.left,
          top: element.style.top,
          wasManuallyPositioned: element.dataset.manuallyPositioned === 'true'
        };
      }
    });
    
    container.innerHTML = '';

    this.placedFurniture.forEach(item => {
      const furnitureElement = this.createRoomFurnitureElement(item);
      
      // Restore previous position if it existed
      if (currentPositions[item.id]) {
        furnitureElement.style.left = currentPositions[item.id].left;
        furnitureElement.style.top = currentPositions[item.id].top;
        furnitureElement.dataset.manuallyPositioned = currentPositions[item.id].wasManuallyPositioned ? 'true' : 'false';
      }
      
      container.appendChild(furnitureElement);
    });
  }

  createRoomFurnitureElement(placedItem) {
    const element = document.createElement('div');
    element.className = `room-furniture furniture-${placedItem.furniture.category}`;
    element.dataset.placedId = placedItem.id;
    element.dataset.manuallyPositioned = 'false'; // Default value

    // Calculate position and size based on current view and room floor
    // Only if not already positioned by renderRoomFurniture
    if (!element.style.left || !element.style.top) {
      this.updateFurnitureElementPosition(element, placedItem);
    }

    element.innerHTML = `
      <span>${placedItem.furniture.name}</span>
      <button class="remove-furniture" onclick="roomPlanner.removeFurniture('${placedItem.id}')">×</button>
    `;

    // Add drag functionality
    this.makeDraggable(element, placedItem);

    return element;
  }

  updateFurnitureElementPosition(element, placedItem) {
    const floor = document.querySelector('.room-floor');
    const roomContainer = document.getElementById('room3d');
    const isTopView = this.currentView === 'top';
    const isFrontView = this.currentView === 'front';
    
    // Get current room dimensions in pixels
    const roomWidthPx = parseFloat(floor.style.width) || 400;
    const roomHeightPx = parseFloat(floor.style.height) || 400;
    
    let furnitureWidthPx, furnitureHeightPx;
    
    if (isTopView) {
      // Top view: use width and depth
      furnitureWidthPx = (placedItem.furniture.dimensions.width / (this.roomSettings.width * 100)) * roomWidthPx;
      furnitureHeightPx = (placedItem.furniture.dimensions.depth / (this.roomSettings.length * 100)) * roomHeightPx;
    } else if (isFrontView) {
      // Front view: use width and height (showing furniture profile)
      furnitureWidthPx = (placedItem.furniture.dimensions.width / (this.roomSettings.width * 100)) * roomWidthPx;
      furnitureHeightPx = (placedItem.furniture.dimensions.height / (this.roomSettings.height * 100)) * 200; // Scale to wall height
    }
    
    // If furniture was manually positioned, preserve its current position
    if (element.dataset.manuallyPositioned === 'true' && element.style.left && element.style.top) {
      // Just update size and rotation, keep position
      element.style.width = furnitureWidthPx + 'px';
      element.style.height = furnitureHeightPx + 'px';
      element.style.transform = `rotate(${isTopView ? placedItem.position.rotation : 0}deg)`;
      return;
    }
    
    // Calculate position relative to floor center only for new/non-manually-positioned furniture
    const floorCenterX = roomContainer.offsetWidth / 2;
    const floorCenterY = roomContainer.offsetHeight / 2;
    
    let furnitureLeft, furnitureTop;
    
    if (isTopView) {
      const floorLeft = floorCenterX - roomWidthPx / 2;
      const floorTop = floorCenterY - roomHeightPx / 2;
      furnitureLeft = floorLeft + (placedItem.position.x / (this.roomSettings.width * 100)) * roomWidthPx;
      furnitureTop = floorTop + (placedItem.position.y / (this.roomSettings.length * 100)) * roomHeightPx;
      
      // Ensure furniture stays within bounds in top view
      const maxLeft = floorLeft + roomWidthPx - furnitureWidthPx;
      const maxTop = floorTop + roomHeightPx - furnitureHeightPx;
      furnitureLeft = Math.max(floorLeft, Math.min(maxLeft, furnitureLeft));
      furnitureTop = Math.max(floorTop, Math.min(maxTop, furnitureTop));
      
    } else if (isFrontView) {
      // In front view, X position becomes horizontal, Y position becomes vertical (from floor)
      const wallWidth = 600; // Match CSS wall width
      const wallHeight = 350; // Match CSS wall height
      // Wall is centered, so calculate its actual position
      const wallTop = floorCenterY - wallHeight / 2;
      const wallBottom = wallTop + wallHeight;
      const wallLeft = floorCenterX - wallWidth / 2;
      const wallRight = wallLeft + wallWidth;
      
      furnitureLeft = wallLeft + (placedItem.position.x / (this.roomSettings.width * 100)) * wallWidth;
      // Floor line is at the bottom of the wall, furniture should sit just above it
      furnitureTop = wallBottom - furnitureHeightPx; // Place furniture bottom at wall bottom
      
      // Ensure furniture stays within wall bounds in front view
      const maxLeft = wallRight - furnitureWidthPx;
      furnitureLeft = Math.max(wallLeft, Math.min(maxLeft, furnitureLeft));
      
      // Ensure furniture doesn't float above floor or sink below
      const maxTop = wallBottom - furnitureHeightPx; // Keep furniture bottom at wall bottom
      const minTop = wallTop;
      furnitureTop = Math.max(minTop, Math.min(maxTop, furnitureTop));
    }

    element.style.cssText = `
      position: absolute;
      width: ${furnitureWidthPx}px;
      height: ${furnitureHeightPx}px;
      left: ${furnitureLeft}px;
      top: ${furnitureTop}px;
      transform: rotate(${isTopView ? placedItem.position.rotation : 0}deg);
      z-index: 5;
    `;
  }

  makeDraggable(element, placedItem) {
    let isDragging = false;
    let startX, startY, startLeft, startTop;

    element.addEventListener('mousedown', (e) => {
      if (e.target.classList.contains('remove-furniture')) return;
      
      isDragging = true;
      element.classList.add('dragging');
      
      startX = e.clientX;
      startY = e.clientY;
      startLeft = element.offsetLeft;
      startTop = element.offsetTop;
      
      e.preventDefault();
    });

    document.addEventListener('mousemove', (e) => {
      if (!isDragging) return;

      const deltaX = e.clientX - startX;
      const deltaY = e.clientY - startY;
      
      let newLeft = startLeft + deltaX;
      let newTop = startTop + deltaY;
      
      // Get room boundaries based on current view
      const floor = document.querySelector('.room-floor');
      const roomContainer = document.getElementById('room3d');
      const roomWidthPx = parseFloat(floor.style.width) || 400;
      const roomHeightPx = parseFloat(floor.style.height) || 400;
      
      const floorCenterX = roomContainer.offsetWidth / 2;
      const floorCenterY = roomContainer.offsetHeight / 2;
      
      let minLeft, maxLeft, minTop, maxTop;
      
      if (this.currentView === 'top') {
        // Top view boundaries
        const floorLeft = floorCenterX - roomWidthPx / 2;
        const floorTop = floorCenterY - roomHeightPx / 2;
        const floorRight = floorLeft + roomWidthPx;
        const floorBottom = floorTop + roomHeightPx;
        
        minLeft = floorLeft;
        maxLeft = floorRight - element.offsetWidth;
        minTop = floorTop;
        maxTop = floorBottom - element.offsetHeight;
      } else if (this.currentView === 'front') {
        // Front view boundaries - only horizontal movement, furniture stays on "floor"
        const wallWidth = 600; // Match CSS wall width
        const wallHeight = 350; // Match CSS wall height
        const wallLeft = floorCenterX - wallWidth / 2;
        const wallRight = wallLeft + wallWidth;
        const wallTop = floorCenterY - wallHeight / 2;
        const wallBottom = wallTop + wallHeight;
        
        minLeft = wallLeft;
        maxLeft = wallRight - element.offsetWidth;
        minTop = wallBottom - element.offsetHeight; // Keep furniture bottom at wall bottom
        maxTop = wallBottom - element.offsetHeight; // Keep furniture bottom at wall bottom
        
        // In front view, only allow horizontal dragging
        newTop = minTop;
      }
      
      const boundedLeft = Math.max(minLeft, Math.min(maxLeft, newLeft));
      const boundedTop = Math.max(minTop, Math.min(maxTop, newTop));
      
      element.style.left = boundedLeft + 'px';
      element.style.top = boundedTop + 'px';
    });

    document.addEventListener('mouseup', () => {
      if (!isDragging) return;
      
      isDragging = false;
      element.classList.remove('dragging');
      
      // Mark element as manually positioned
      element.dataset.manuallyPositioned = 'true';
      
      // Update position in data based on current view
      const floor = document.querySelector('.room-floor');
      const roomContainer = document.getElementById('room3d');
      const roomWidthPx = parseFloat(floor.style.width) || 400;
      const roomHeightPx = parseFloat(floor.style.height) || 400;
      
      const floorCenterX = roomContainer.offsetWidth / 2;
      const floorCenterY = roomContainer.offsetHeight / 2;
      
      if (this.currentView === 'top') {
        const floorLeft = floorCenterX - roomWidthPx / 2;
        const floorTop = floorCenterY - roomHeightPx / 2;
        
        const relativeLeft = element.offsetLeft - floorLeft;
        const relativeTop = element.offsetTop - floorTop;
        
        placedItem.position.x = (relativeLeft / roomWidthPx) * (this.roomSettings.width * 100);
        placedItem.position.y = (relativeTop / roomHeightPx) * (this.roomSettings.length * 100);
      } else if (this.currentView === 'front') {
        // In front view, only update X position
        const wallLeft = floorCenterX - roomWidthPx / 2;
        const relativeLeft = element.offsetLeft - wallLeft;
        
        placedItem.position.x = (relativeLeft / roomWidthPx) * (this.roomSettings.width * 100);
        // Y position stays the same in front view
      }
    });
  }

  removeFurniture(placedId) {
    this.placedFurniture = this.placedFurniture.filter(item => item.id !== placedId);
    this.renderRoomFurniture();
    this.updateFurnitureList();
    this.updateStats();
  }

  validateFurniturePositions() {
    this.placedFurniture.forEach(placedItem => {
      const roomWidthCm = this.roomSettings.width * 100;
      const roomLengthCm = this.roomSettings.length * 100;
      
      // Ensure furniture doesn't exceed room boundaries
      const maxX = roomWidthCm - placedItem.furniture.dimensions.width;
      const maxY = roomLengthCm - placedItem.furniture.dimensions.depth;
      
      // Fix position if out of bounds
      if (placedItem.position.x > maxX) {
        placedItem.position.x = Math.max(0, maxX);
      }
      if (placedItem.position.y > maxY) {
        placedItem.position.y = Math.max(0, maxY);
      }
      if (placedItem.position.x < 0) {
        placedItem.position.x = 0;
      }
      if (placedItem.position.y < 0) {
        placedItem.position.y = 0;
      }
    });
  }

  updateRoomDisplay() {
    const floor = document.querySelector('.room-floor');
    const scaleX = this.roomSettings.width / 4;
    const scaleY = this.roomSettings.length / 4;
    
    // Update floor size
    floor.style.width = `${400 * scaleX}px`;
    floor.style.height = `${400 * scaleY}px`;
    
    // Add dimensions label
    floor.setAttribute('data-dimensions', `${this.roomSettings.width}×${this.roomSettings.length} מטר`);
    
    // Update room size display
    this.updateRoomSizeDisplay();
    
    // Validate furniture positions after room size change
    this.validateFurniturePositions();
    
    // Re-render furniture with new room size
    this.renderRoomFurniture();
  }

  updateFurnitureList() {
    const list = document.getElementById('furnitureList');
    
    if (this.placedFurniture.length === 0) {
      list.innerHTML = `
        <div class="empty-state">
          <p>עדיין לא הוספת רהיטים לחדר</p>
          <p>בחר פריטים מהקטלוג בצד שמאל</p>
        </div>
      `;
      return;
    }

    list.innerHTML = this.placedFurniture.map(item => `
      <div class="furniture-list-item" style="
        padding: 10px;
        margin-bottom: 8px;
        background: white;
        border: 1px solid var(--planner-light-gray);
        border-radius: 4px;
        display: flex;
        justify-content: space-between;
        align-items: center;
      ">
        <span style="font-weight: 500;">${item.furniture.name}</span>
        <button onclick="roomPlanner.removeFurniture('${item.id}')" style="
          padding: 4px 8px;
          background: #ef4444;
          color: white;
          border: none;
          border-radius: 3px;
          font-size: 0.8rem;
          cursor: pointer;
        ">הסר</button>
      </div>
    `).join('');
  }

  updateStats() {
    const area = this.roomSettings.width * this.roomSettings.length;
    const volume = area * this.roomSettings.height;
    
    document.getElementById('roomArea').textContent = `${area.toFixed(1)} מ"ר`;
    document.getElementById('roomVolume').textContent = `${volume.toFixed(1)} מ"ק`;
    document.getElementById('furnitureCount').textContent = this.placedFurniture.length;
  }

  bindEvents() {
    // Room settings
    document.getElementById('applyRoomSettings').addEventListener('click', () => {
      this.roomSettings.width = parseFloat(document.getElementById('roomWidth').value);
      this.roomSettings.length = parseFloat(document.getElementById('roomLength').value);
      this.roomSettings.height = parseFloat(document.getElementById('roomHeight').value);
      
      this.updateRoomDisplay();
      this.updateStats();
    });

    // Category filters
    document.querySelectorAll('.category-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.category-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        this.renderFurnitureGrid();
      });
    });

    // Search
    document.getElementById('searchInput').addEventListener('input', () => {
      this.renderFurnitureGrid();
    });

    // View controls
    document.querySelectorAll('.view-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.view-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        this.currentView = btn.dataset.view;
        this.changeView();
      });
    });

    // Reset button
    document.querySelector('.reset-btn').addEventListener('click', () => {
      if (confirm('האם אתה בטוח שברצונך לאפס את הפרויקט?')) {
        this.placedFurniture = [];
        this.renderRoomFurniture();
        this.updateFurnitureList();
        this.updateStats();
      }
    });

    // Export buttons
    document.querySelector('.export-btn').addEventListener('click', () => {
      this.generateQuote();
    });
  }

  changeView() {
    const room3d = document.getElementById('room3d');
    
    // Remove all view classes
    room3d.classList.remove('view-top', 'view-front');
    
    // Add the current view class
    room3d.classList.add(`view-${this.currentView}`);
    
    // Validate furniture positions before updating view
    this.validateFurniturePositions();
    
    // Update furniture positioning for the new view
    setTimeout(() => {
      this.placedFurniture.forEach(item => {
        const element = document.querySelector(`[data-placed-id="${item.id}"]`);
        if (element) {
          this.updateFurnitureElementPosition(element, item);
        }
      });
    }, 100);
  }

  generateQuote() {
    const roomDetails = `
חדר: ${this.roomSettings.width}×${this.roomSettings.length}×${this.roomSettings.height} מטר
שטח: ${(this.roomSettings.width * this.roomSettings.length).toFixed(1)} מ"ר

רהיטים:
${this.placedFurniture.map(item => `• ${item.furniture.name} (${item.furniture.dimensions.width}×${item.furniture.dimensions.depth}×${item.furniture.dimensions.height} ס"מ)`).join('\n')}

נוצר ב: ${new Date().toLocaleDateString('he-IL')}
    `.trim();

    // Create a temporary element to copy text
    const textarea = document.createElement('textarea');
    textarea.value = roomDetails;
    document.body.appendChild(textarea);
    textarea.select();
    document.execCommand('copy');
    document.body.removeChild(textarea);

    alert('פרטי הפרויקט הועתקו! אתה יכול לשלוח אותם למעצבת.');
  }

  clearRoom() {
    if (confirm('האם אתה בטוח שברצונך להסיר את כל הרהיטים מהחדר?')) {
      this.placedFurniture = [];
      this.renderRoomFurniture();
      this.updateFurnitureList();
      this.updateStats();
    }
  }

  centerView() {
    const room3d = document.getElementById('room3d');
    const floor = document.querySelector('.room-floor');
    
    // Reset any transforms and center the view
    room3d.scrollTop = 0;
    room3d.scrollLeft = 0;
    
    // Animate a subtle highlight of the room
    floor.style.transition = 'all 0.5s ease';
    floor.style.transform = 'translate(-50%, -50%) scale(1.05)';
    floor.style.boxShadow = '0 0 30px rgba(74, 144, 226, 0.5)';
    
    setTimeout(() => {
      floor.style.transform = 'translate(-50%, -50%) scale(1)';
      floor.style.boxShadow = '0 0 20px rgba(0,0,0,0.1)';
    }, 500);
  }

  updateRoomSizeDisplay() {
    const display = document.getElementById('roomSizeDisplay');
    if (display) {
      display.textContent = `חדר: ${this.roomSettings.width}×${this.roomSettings.length} מטר`;
    }
  }
}

// Add CSS for animations
const style = document.createElement('style');
style.textContent = `
  @keyframes fadeInOut {
    0% { opacity: 0; transform: translate(-50%, -50%) scale(0.8); }
    20% { opacity: 1; transform: translate(-50%, -50%) scale(1); }
    80% { opacity: 1; transform: translate(-50%, -50%) scale(1); }
    100% { opacity: 0; transform: translate(-50%, -50%) scale(0.8); }
  }
`;
document.head.appendChild(style);

// Initialize room planner when page loads
let roomPlanner;
document.addEventListener('DOMContentLoaded', () => {
  roomPlanner = new RoomPlanner();
});
