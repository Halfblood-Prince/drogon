const utcTime = document.querySelector("#utc-time");

function tick() {
  const now = new Date();
  const formatter = new Intl.DateTimeFormat("en-US", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
    timeZone: "UTC"
  });
  utcTime.textContent = formatter.format(now);
}

tick();
setInterval(tick, 1000);

document.querySelectorAll(".metric strong").forEach((node, index) => {
  node.style.animationDelay = `${index * 45}ms`;
});
