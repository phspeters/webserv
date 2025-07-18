// script.js

// Wait until the DOM is fully loaded
document.addEventListener("DOMContentLoaded", function () {
    const button = document.getElementById("clickButton");
    const output = document.getElementById("output");
  
    button.addEventListener("click", function () {
      const currentTime = new Date().toLocaleTimeString();
      output.textContent = "Button clicked at: " + currentTime;
    });
  });
  