document.addEventListener('DOMContentLoaded', e => {
  setInterval(() => {
    document.querySelector('h1').classList.toggle('red')
  }, 1000)
})