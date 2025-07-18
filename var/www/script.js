// Wait for the DOM to fully load
document.addEventListener("DOMContentLoaded", function () {
    // Example 1: Toggle navigation menu on mobile
    const menuButton = document.getElementById("menu-button");
    const nav = document.querySelector("nav");
  
    if (menuButton) {
      menuButton.addEventListener("click", function () {
        nav.classList.toggle("active");
      });
    }
  
    // Example 2: Alert on button click
    const alertButton = document.getElementById("alert-button");
  
    if (alertButton) {
      alertButton.addEventListener("click", function () {
        alert("You clicked the button!");
      });
    }
  
    // Example 3: Form validation
    const form = document.getElementById("contact-form");
  
    if (form) {
      form.addEventListener("submit", function (e) {
        const nameInput = document.getElementById("name");
  
        if (nameInput && nameInput.value.trim() === "") {
          e.preventDefault();
          alert("Please enter your name.");
        }
      });
    }
  });
  